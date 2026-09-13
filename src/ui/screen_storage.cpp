#include "screen_storage.h"
#include "nav.h"
#include "../services/sdcard_service.h"
#include "../services/audio_service.h"
#include "esp_lvgl_port.h"
// avi_player's exact struct/function names are checked against whatever version the component
// manager resolves on first `pio run` — see docs/BRINGUP.md "Video playback".
#include "avi_player.h"
// avi_player only demuxes the AVI container — frame_data_t.data for a video frame is still
// COMPRESSED JPEG bytes, not raw pixels (confirmed on hardware: handing that buffer straight to
// lv_canvas_set_buffer() as if it were already-decoded RGB565 crashed with a Load access fault
// inside LVGL's blend code, reading far past the ~28KB compressed buffer as if it were the
// ~768KB a raw 480x800 RGB565 frame would be). Decode each frame with the ESP32-P4's hardware
// JPEG decoder before handing it to the canvas. See docs/BRINGUP.md "Video playback".
#include "driver/jpeg_decode.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace screen_storage {

static lv_obj_t *s_info_label = nullptr;
static lv_obj_t *s_list = nullptr;
static lv_obj_t *s_video_overlay = nullptr;
static lv_obj_t *s_video_canvas = nullptr;
static avi_player_handle_t s_player = nullptr;
static jpeg_decoder_handle_t s_jpeg_decoder = nullptr;
static uint8_t *s_jpeg_out_buf = nullptr;
static size_t s_jpeg_out_buf_size = 0;

// avi_player runs ONE task that demuxes the file and calls video_cb/audio_cb synchronously and
// sequentially. Doing the real work (JPEG decode + LVGL canvas update; blocking I2S write) right
// inside those callbacks — the original, simpler implementation — serializes video and audio
// behind each other: a slow video frame directly delays the very next audio chunk, and vice
// versa. Confirmed on hardware: both audio and video were choppy together even after fixing the
// individual bugs in each (color order, buffer size, LVGL flush cost) — the remaining problem
// was this shared-task serialization itself, not any single slow step.
//
// Fix: decouple into two independent consumer tasks, fed by the (fast, just memcpy) callbacks:
//   - Video: a single-slot "latest frame wins" mailbox. If the render task falls behind, the
//     next arriving frame simply replaces whatever's pending — old frames are dropped rather
//     than queued up, so playback never accumulates lag, at the cost of skipping frames under
//     load. Video quality degrading gracefully under load is normal/expected for a decode
//     pipeline that's fundamentally close to this SoC's real-time throughput ceiling for
//     480x800 JPEG frames (~40ms hardware-decode floor alone, measured on hardware).
//   - Audio: a short FIFO queue, so its own task can call the blocking, real-time-pacing
//     audio_service::play_pcm() promptly and continuously, independent of how far behind video
//     rendering is. This is the fix for the choppy *audio* specifically — audio needs in-order,
//     gap-free delivery (unlike video, dropping an audio chunk is audible and worse than
//     dropping a video frame), so it always gets consumed, never dropped from the callback side.
// See docs/BRINGUP.md "Video playback".

struct AudioChunk {
    uint8_t *data;
    size_t len;
    uint32_t sample_rate;
    uint8_t channel;
};

static SemaphoreHandle_t s_video_mutex = nullptr;
static SemaphoreHandle_t s_video_signal = nullptr;
static uint8_t *s_pending_jpeg = nullptr;
static size_t s_pending_jpeg_len = 0;
static uint32_t s_pending_w = 0, s_pending_h = 0;
static QueueHandle_t s_audio_queue = nullptr;
static volatile bool s_stop_render_tasks = false;

static void video_render_task(void *arg) {
    (void)arg;
    for (;;) {
        if (xSemaphoreTake(s_video_signal, pdMS_TO_TICKS(200)) != pdTRUE) {
            if (s_stop_render_tasks) break;
            continue;
        }
        if (s_stop_render_tasks) break;

        xSemaphoreTake(s_video_mutex, portMAX_DELAY);
        uint8_t *jpeg = s_pending_jpeg;
        size_t jpeg_len = s_pending_jpeg_len;
        uint32_t w = s_pending_w, h = s_pending_h;
        s_pending_jpeg = nullptr;
        xSemaphoreGive(s_video_mutex);

        if (!jpeg) continue;

        if (s_jpeg_decoder && s_jpeg_out_buf) {
            jpeg_decode_cfg_t decode_cfg = {};
            decode_cfg.output_format = JPEG_DECODE_OUT_FORMAT_RGB565;
            // BGR, not RGB: colors looked "very weird" (channel-swapped) on real hardware with
            // RGB — this hardware decoder's element order and LVGL's RGB565 canvas expectation
            // don't match the way the naming would suggest. See docs/BRINGUP.md "Video playback".
            decode_cfg.rgb_order = JPEG_DEC_RGB_ELEMENT_ORDER_BGR;
            decode_cfg.conv_std = JPEG_YUV_RGB_CONV_STD_BT601;

            uint32_t out_size = 0;
            esp_err_t err = jpeg_decoder_process(s_jpeg_decoder, &decode_cfg, jpeg, jpeg_len, s_jpeg_out_buf,
                                                  s_jpeg_out_buf_size, &out_size);
            if (err == ESP_OK && lvgl_port_lock(0)) {
                lv_canvas_set_buffer(s_video_canvas, s_jpeg_out_buf, w, h, LV_COLOR_FORMAT_RGB565);
                lvgl_port_unlock();
            }
        }
        heap_caps_free(jpeg);
    }
    vTaskDelete(nullptr);
}

static void audio_play_task(void *arg) {
    (void)arg;
    AudioChunk chunk;
    for (;;) {
        if (xQueueReceive(s_audio_queue, &chunk, pdMS_TO_TICKS(200)) != pdTRUE) {
            if (s_stop_render_tasks) break;
            continue;
        }
        if (chunk.data) {
            audio_service::play_pcm(chunk.data, chunk.len, chunk.sample_rate, chunk.channel);
            heap_caps_free(chunk.data);
        }
    }
    vTaskDelete(nullptr);
}

static void video_frame_cb(frame_data_t *data, void *arg) {
    (void)arg;
    if (!data || data->type != FRAME_TYPE_VIDEO || !s_video_mutex) return;
    // MALLOC_CAP_SPIRAM, not plain malloc() — found crashing on hardware otherwise. This buffer
    // is the JPEG *input* handed straight to the hardware decoder's 2D-DMA engine, which does a
    // cache-sync (esp_cache_msync) on it before reading; that sync asserts if the buffer isn't
    // in PSRAM (plain malloc() can land in internal SRAM). The original implementation never hit
    // this because it decoded avi_player's own (PSRAM-backed) buffer in place; copying the frame
    // out for the mailbox introduced this new buffer and needed the same placement.
    // See docs/BRINGUP.md "Video playback".
    auto *copy = static_cast<uint8_t *>(heap_caps_malloc(data->data_bytes, MALLOC_CAP_SPIRAM));
    if (!copy) return;
    memcpy(copy, data->data, data->data_bytes);

    xSemaphoreTake(s_video_mutex, portMAX_DELAY);
    if (s_pending_jpeg) heap_caps_free(s_pending_jpeg); // render task is behind — drop the stale frame
    s_pending_jpeg = copy;
    s_pending_jpeg_len = data->data_bytes;
    s_pending_w = data->video_info.width;
    s_pending_h = data->video_info.height;
    xSemaphoreGive(s_video_mutex);
    xSemaphoreGive(s_video_signal);
}

static void audio_frame_cb(frame_data_t *data, void *arg) {
    (void)arg;
    // Unlike video, AVI audio frames are already raw PCM (FORMAT_PCM is the only value the
    // format enum has) — no decode step needed, just copy and queue the bytes. PSRAM here too,
    // for consistency (not strictly required — audio doesn't go through the JPEG/2DDMA path —
    // but keeps these sizable buffers off internal SRAM).
    if (!data || data->type != FRAME_TYPE_AUDIO || !s_audio_queue) return;
    auto *copy = static_cast<uint8_t *>(heap_caps_malloc(data->data_bytes, MALLOC_CAP_SPIRAM));
    if (!copy) return;
    memcpy(copy, data->data, data->data_bytes);
    AudioChunk chunk{copy, data->data_bytes, data->audio_info.sample_rate, data->audio_info.channel};
    if (xQueueSend(s_audio_queue, &chunk, pdMS_TO_TICKS(1000)) != pdTRUE) {
        heap_caps_free(copy); // consumer genuinely stuck — drop rather than block the demux thread forever
    }
}

static bool s_closing = false;

static void close_video_task(void *arg) {
    (void)arg;
    if (s_player) {
        avi_player_play_stop(s_player);
        avi_player_deinit(s_player);
        s_player = nullptr;
    }

    s_stop_render_tasks = true;
    if (s_video_signal) xSemaphoreGive(s_video_signal); // wake video_render_task if it's waiting
    vTaskDelay(pdMS_TO_TICKS(300)); // let both consumer tasks notice the stop flag and self-delete

    if (s_video_mutex) {
        vSemaphoreDelete(s_video_mutex);
        s_video_mutex = nullptr;
    }
    if (s_video_signal) {
        vSemaphoreDelete(s_video_signal);
        s_video_signal = nullptr;
    }
    if (s_pending_jpeg) {
        heap_caps_free(s_pending_jpeg);
        s_pending_jpeg = nullptr;
    }
    if (s_audio_queue) {
        AudioChunk leftover;
        while (xQueueReceive(s_audio_queue, &leftover, 0) == pdTRUE) {
            if (leftover.data) heap_caps_free(leftover.data);
        }
        vQueueDelete(s_audio_queue);
        s_audio_queue = nullptr;
    }

    if (s_jpeg_decoder) {
        jpeg_del_decoder_engine(s_jpeg_decoder);
        s_jpeg_decoder = nullptr;
    }
    if (s_jpeg_out_buf) {
        // jpeg_alloc_decoder_mem() is a plain heap_caps_calloc() under the hood — it's not
        // owned/tracked by the decoder engine, so it needs its own explicit free.
        heap_caps_free(s_jpeg_out_buf);
        s_jpeg_out_buf = nullptr;
    }
    s_jpeg_out_buf_size = 0;
    if (lvgl_port_lock(0)) {
        if (s_video_overlay) {
            lv_obj_del(s_video_overlay);
            s_video_overlay = nullptr;
            s_video_canvas = nullptr;
        }
        lvgl_port_unlock();
    }
    s_closing = false;
    vTaskDelete(nullptr);
}

static void close_video(lv_event_t *e) {
    (void)e;
    if (s_closing) return;
    s_closing = true;
    // Deferred to a separate task — found deterministically freezing the device on hardware
    // otherwise. This callback runs from LVGL's own event dispatch, which holds the
    // lvgl_port_lock()-managed lock for the whole callback. avi_player_play_stop() blocks
    // waiting for the decode task to acknowledge the stop, but that task can itself be blocked
    // inside video_frame_cb() trying to acquire that very same lock — a deadlock if both happen
    // on the same lock at once. Returning immediately here (after spawning this task) lets the
    // LVGL dispatch finish and release the lock, so the decode task can actually make progress
    // and be stopped cleanly. See docs/BRINGUP.md "Video playback".
    xTaskCreate(close_video_task, "close_video", 4096, nullptr, 5, nullptr);
}

static void play_video(const std::string &path) {
    s_video_overlay = lv_obj_create(lv_scr_act());
    lv_obj_remove_style_all(s_video_overlay);
    lv_obj_set_size(s_video_overlay, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(s_video_overlay, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_video_overlay, LV_OPA_COVER, LV_PART_MAIN);

    s_video_canvas = lv_canvas_create(s_video_overlay);
    lv_obj_center(s_video_canvas);

    lv_obj_t *close_btn = lv_btn_create(s_video_overlay);
    lv_obj_set_size(close_btn, 60, 50);
    lv_obj_align(close_btn, LV_ALIGN_TOP_RIGHT, -10, 10);
    lv_obj_add_event_cb(close_btn, close_video, LV_EVENT_CLICKED, nullptr);
    lv_obj_t *lbl = lv_label_create(close_btn);
    lv_label_set_text(lbl, LV_SYMBOL_CLOSE);
    lv_obj_center(lbl);

    jpeg_decode_engine_cfg_t jpeg_eng_cfg = {};
    jpeg_eng_cfg.intr_priority = 0;
    jpeg_eng_cfg.timeout_ms = 200;
    if (jpeg_new_decoder_engine(&jpeg_eng_cfg, &s_jpeg_decoder) != ESP_OK) {
        s_jpeg_decoder = nullptr;
        return;
    }

    jpeg_decode_memory_alloc_cfg_t mem_cfg = {};
    mem_cfg.buffer_direction = JPEG_DEC_ALLOC_OUTPUT_BUFFER;
    size_t allocated = 0;
    // 480x800 RGB565 (2 bytes/pixel) covers every frame in the vendor sample clips; sized generously
    // since actual video dimensions vary per file and the driver pads to a multiple of 16 anyway.
    s_jpeg_out_buf = static_cast<uint8_t *>(jpeg_alloc_decoder_mem(480 * 800 * 2, &mem_cfg, &allocated));
    s_jpeg_out_buf_size = allocated;
    if (!s_jpeg_out_buf) {
        jpeg_del_decoder_engine(s_jpeg_decoder);
        s_jpeg_decoder = nullptr;
        return;
    }

    s_stop_render_tasks = false;
    s_video_mutex = xSemaphoreCreateMutex();
    s_video_signal = xSemaphoreCreateBinary();
    s_audio_queue = xQueueCreate(8, sizeof(AudioChunk));
    // Video render task shares core 1 with avi_player's own demux task (deliberately — its
    // per-frame work is now just a mutex-guarded pointer swap, cheap enough not to starve
    // demuxing). Audio gets its own core (0) and a higher priority so scheduling contention from
    // video/demux work can't delay it — audio smoothness was the main complaint being fixed here.
    xTaskCreatePinnedToCore(video_render_task, "video_render", 4096, nullptr, 5, nullptr, 1);
    xTaskCreatePinnedToCore(audio_play_task, "audio_play", 4096, nullptr, 6, nullptr, 0);

    avi_player_config_t cfg = {};
    cfg.video_cb = video_frame_cb;
    cfg.audio_cb = audio_frame_cb;
    cfg.priority = 5;
    cfg.stack_size = 8192;
    cfg.coreID = 1;
    // Default (0 -> 20KB internal) is too small for a single 480x800 compressed JPEG frame from
    // the vendor sample clips — confirmed on hardware ("frame size 28044 exceeds available
    // data", then parser desync into garbage frame sizes). See docs/BRINGUP.md "Video playback".
    cfg.buffer_size = 100 * 1024;

    if (avi_player_init(cfg, &s_player) == ESP_OK) {
        avi_player_play_from_file(s_player, path.c_str());
    }
}

static std::string s_current_rel_path = ""; // relative to MOUNT_POINT; "" = root

static void refresh_list(const std::string &rel_path);

struct RowInfo {
    std::string rel_path;  // relative to MOUNT_POINT, for navigating into a directory
    std::string full_path; // absolute filesystem path, for opening a file
    bool is_dir;
};

static void row_click_cb(lv_event_t *e) {
    auto *info = static_cast<RowInfo *>(lv_event_get_user_data(e));
    if (info->is_dir) {
        s_current_rel_path = info->rel_path;
        refresh_list(s_current_rel_path);
    } else {
        std::string lower = info->full_path;
        std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
        if (lower.size() > 4 && lower.substr(lower.size() - 4) == ".avi") {
            play_video(info->full_path);
        }
    }
    // Don't delete `info` here — refresh_list() above may have already deleted this very row
    // (and its RowInfo) via lv_obj_clean(). Freed instead in row_delete_cb below, which LVGL
    // calls exactly once per row when it's actually destroyed.
}

static void row_delete_cb(lv_event_t *e) {
    delete static_cast<RowInfo *>(lv_event_get_user_data(e));
}

static void refresh_list(const std::string &rel_path) {
    lv_obj_clean(s_list);
    std::vector<sdcard_service::FileEntry> entries;
    if (sdcard_service::list_dir(rel_path, entries) != ESP_OK) {
        lv_list_add_text(s_list, "Could not read SD card. Is one inserted (FAT32, <32GB)?");
        return;
    }
    std::sort(entries.begin(), entries.end(), [](const auto &a, const auto &b) { return a.name < b.name; });

    if (!rel_path.empty()) {
        lv_obj_t *up_btn = lv_list_add_btn(s_list, LV_SYMBOL_UP, ".. (up)");
        auto *up_info = new RowInfo{};
        size_t slash = rel_path.find_last_of('/');
        up_info->rel_path = (slash == std::string::npos) ? "" : rel_path.substr(0, slash);
        up_info->is_dir = true;
        lv_obj_add_event_cb(up_btn, row_click_cb, LV_EVENT_CLICKED, up_info);
        lv_obj_add_event_cb(up_btn, row_delete_cb, LV_EVENT_DELETE, up_info);
    }

    for (auto &fe : entries) {
        char row[160];
        if (fe.is_dir) {
            snprintf(row, sizeof(row), "%s", fe.name.c_str());
        } else {
            snprintf(row, sizeof(row), "%s  (%u KB)", fe.name.c_str(), static_cast<unsigned>(fe.size / 1024));
        }
        lv_obj_t *btn = lv_list_add_btn(s_list, fe.is_dir ? LV_SYMBOL_DIRECTORY : LV_SYMBOL_FILE, row);
        auto *info = new RowInfo{};
        info->rel_path = rel_path + "/" + fe.name;
        info->full_path = std::string(sdcard_service::MOUNT_POINT) + rel_path + "/" + fe.name;
        info->is_dir = fe.is_dir;
        lv_obj_add_event_cb(btn, row_click_cb, LV_EVENT_CLICKED, info);
        lv_obj_add_event_cb(btn, row_delete_cb, LV_EVENT_DELETE, info);
    }
}

lv_obj_t *create() {
    lv_obj_t *scr = lv_obj_create(nullptr);
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x181825), LV_PART_MAIN);
    nav::add_back_button(scr);

    lv_obj_t *title = lv_label_create(scr);
    lv_label_set_text(title, "Storage & Video");
    lv_obj_set_style_text_color(title, lv_color_hex(0xCDD6F4), LV_PART_MAIN);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_32, LV_PART_MAIN);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 65);

    s_info_label = lv_label_create(scr);
    lv_obj_set_style_text_color(s_info_label, lv_color_hex(0xA6E3A1), LV_PART_MAIN);
    // Text set BEFORE aligning — lv_obj_align_to() computes the centered position from the
    // label's width at the moment it's called, and an empty label is zero-width. Aligning first
    // and setting text after "centered" a zero-width box, then the real text just grew
    // rightward from that point instead of being centered — found via on-hardware testing (the
    // info line and, since it chained off this one, the file list below it both drifted right).
    uint64_t total = 0, free_b = 0;
    if (sdcard_service::get_space(total, free_b) == ESP_OK) {
        lv_label_set_text_fmt(s_info_label, "%llu MB free / %llu MB total", free_b / (1024ULL * 1024),
                                total / (1024ULL * 1024));
    } else {
        lv_label_set_text(s_info_label, "No SD card detected.");
    }
    lv_obj_align_to(s_info_label, title, LV_ALIGN_OUT_BOTTOM_MID, 0, 10);

    s_list = lv_list_create(scr);
    lv_obj_set_size(s_list, LV_PCT(90), 500);
    lv_obj_align_to(s_list, s_info_label, LV_ALIGN_OUT_BOTTOM_MID, 0, 15);

    s_current_rel_path = "";
    refresh_list(s_current_rel_path);

    return scr;
}

} // namespace screen_storage
