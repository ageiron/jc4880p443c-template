#include "screen_audio.h"
#include "nav.h"
#include "../services/audio_service.h"
#include "esp_lvgl_port.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace screen_audio {

static lv_obj_t *s_level_bar = nullptr;
static lv_obj_t *s_loop_btn_lbl = nullptr;
static lv_timer_t *s_meter_timer = nullptr;
static bool s_loop_busy = false;

static void meter_timer_cb(lv_timer_t *timer) {
    (void)timer;
    if (!audio_service::is_capturing()) return;
    int level = audio_service::get_input_level();
    lv_bar_set_value(s_level_bar, level, LV_ANIM_ON);
}

static void tone_btn_cb(lv_event_t *e) {
    (void)e;
    audio_service::play_tone(440, 400); // A4 for 400ms — quick "speaker works" proof
}

static void volume_slider_cb(lv_event_t *e) {
    lv_obj_t *slider = static_cast<lv_obj_t *>(lv_event_get_target(e));
    audio_service::set_volume(lv_slider_get_value(slider));
}

static void mic_toggle_cb(lv_event_t *e) {
    lv_obj_t *sw = static_cast<lv_obj_t *>(lv_event_get_target(e));
    if (lv_obj_has_state(sw, LV_STATE_CHECKED)) {
        audio_service::start_capture();
    } else {
        audio_service::stop_capture();
        lv_bar_set_value(s_level_bar, 0, LV_ANIM_OFF);
    }
}

struct LoopBuf {
    int16_t *pcm;
    size_t max_samples;
};

static void loop_task(void *arg) {
    auto *buf = static_cast<LoopBuf *>(arg);
    const uint32_t sample_rate = 16000;

    if (lvgl_port_lock(0)) {
        lv_label_set_text(s_loop_btn_lbl, "Recording (3s)...");
        lvgl_port_unlock();
    }

    audio_service::start_capture();
    size_t total = 0;
    const size_t chunk = 512;
    while (total + chunk <= buf->max_samples) {
        size_t bytes_read = 0;
        esp_err_t err = audio_service::read_capture(reinterpret_cast<uint8_t *>(buf->pcm + total),
                                                      chunk * sizeof(int16_t), &bytes_read);
        if (err != ESP_OK) break;
        total += bytes_read / sizeof(int16_t);
    }
    audio_service::stop_capture();

    if (lvgl_port_lock(0)) {
        lv_label_set_text(s_loop_btn_lbl, "Playing back...");
        lvgl_port_unlock();
    }

    audio_service::play_pcm(reinterpret_cast<uint8_t *>(buf->pcm), total * sizeof(int16_t), sample_rate);

    heap_caps_free(buf->pcm);
    delete buf;
    s_loop_busy = false;

    if (lvgl_port_lock(0)) {
        lv_label_set_text(s_loop_btn_lbl, "Record & Playback (3s)");
        lvgl_port_unlock();
    }
    vTaskDelete(nullptr);
}

static void loop_btn_cb(lv_event_t *e) {
    (void)e;
    if (s_loop_busy) return;
    s_loop_busy = true;

    const uint32_t sample_rate = 16000;
    const uint32_t seconds = 3;
    size_t max_samples = sample_rate * seconds;
    auto *buf = new LoopBuf{
        static_cast<int16_t *>(heap_caps_malloc(max_samples * sizeof(int16_t), MALLOC_CAP_SPIRAM)), max_samples};
    if (!buf->pcm) {
        delete buf;
        s_loop_busy = false;
        lv_label_set_text(s_loop_btn_lbl, "Out of memory");
        return;
    }
    xTaskCreate(loop_task, "audio_loop", 4096, buf, 5, nullptr);
}

static void screen_delete_cb(lv_event_t *e) {
    (void)e;
    if (s_meter_timer) {
        lv_timer_del(s_meter_timer);
        s_meter_timer = nullptr;
    }
    if (audio_service::is_capturing()) audio_service::stop_capture();
}

lv_obj_t *create() {
    lv_obj_t *scr = lv_obj_create(nullptr);
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x181825), LV_PART_MAIN);
    lv_obj_add_event_cb(scr, screen_delete_cb, LV_EVENT_DELETE, nullptr);
    nav::add_back_button(scr);

    lv_obj_t *title = lv_label_create(scr);
    lv_label_set_text(title, "Audio: Speaker + Mic");
    lv_obj_set_style_text_color(title, lv_color_hex(0xCDD6F4), LV_PART_MAIN);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_32, LV_PART_MAIN);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 30);

    // Speaker section
    lv_obj_t *speaker_lbl = lv_label_create(scr);
    lv_label_set_text(speaker_lbl, "Speaker");
    lv_obj_set_style_text_color(speaker_lbl, lv_color_hex(0xA6E3A1), LV_PART_MAIN);
    lv_obj_align(speaker_lbl, LV_ALIGN_TOP_LEFT, 30, 110);

    lv_obj_t *tone_btn = lv_btn_create(scr);
    lv_obj_set_size(tone_btn, 160, 55);
    lv_obj_align(tone_btn, LV_ALIGN_TOP_LEFT, 30, 140);
    lv_obj_add_event_cb(tone_btn, tone_btn_cb, LV_EVENT_CLICKED, nullptr);
    lv_obj_t *tone_lbl = lv_label_create(tone_btn);
    lv_label_set_text(tone_lbl, "Play Tone");
    lv_obj_center(tone_lbl);

    lv_obj_t *vol_lbl = lv_label_create(scr);
    lv_label_set_text(vol_lbl, "Volume");
    lv_obj_set_style_text_color(vol_lbl, lv_color_hex(0xCDD6F4), LV_PART_MAIN);
    lv_obj_align(vol_lbl, LV_ALIGN_TOP_LEFT, 220, 110);

    lv_obj_t *slider = lv_slider_create(scr);
    lv_obj_set_size(slider, 180, 15);
    lv_obj_align(slider, LV_ALIGN_TOP_LEFT, 220, 155);
    lv_slider_set_range(slider, 0, 100);
    lv_slider_set_value(slider, 70, LV_ANIM_OFF);
    lv_obj_add_event_cb(slider, volume_slider_cb, LV_EVENT_VALUE_CHANGED, nullptr);

    // Mic section
    lv_obj_t *mic_lbl = lv_label_create(scr);
    lv_label_set_text(mic_lbl, "Mic level");
    lv_obj_set_style_text_color(mic_lbl, lv_color_hex(0xA6E3A1), LV_PART_MAIN);
    lv_obj_align(mic_lbl, LV_ALIGN_TOP_LEFT, 30, 230);

    lv_obj_t *mic_sw = lv_switch_create(scr);
    lv_obj_align(mic_sw, LV_ALIGN_TOP_LEFT, 150, 225);
    lv_obj_add_event_cb(mic_sw, mic_toggle_cb, LV_EVENT_VALUE_CHANGED, nullptr);

    s_level_bar = lv_bar_create(scr);
    lv_obj_set_size(s_level_bar, 300, 25);
    lv_obj_align(s_level_bar, LV_ALIGN_TOP_LEFT, 30, 270);
    lv_bar_set_range(s_level_bar, 0, 100);
    lv_obj_set_style_bg_color(s_level_bar, lv_color_hex(0xA6E3A1), LV_PART_INDICATOR);

    s_meter_timer = lv_timer_create(meter_timer_cb, 100, nullptr);

    // Record/playback loop
    lv_obj_t *loop_btn = lv_btn_create(scr);
    lv_obj_set_size(loop_btn, 260, 60);
    lv_obj_align(loop_btn, LV_ALIGN_TOP_MID, 0, 340);
    lv_obj_add_event_cb(loop_btn, loop_btn_cb, LV_EVENT_CLICKED, nullptr);
    s_loop_btn_lbl = lv_label_create(loop_btn);
    lv_label_set_text(s_loop_btn_lbl, "Record & Playback (3s)");
    lv_obj_center(s_loop_btn_lbl);

    s_loop_busy = false;

    return scr;
}

} // namespace screen_audio
