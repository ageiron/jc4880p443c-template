#include "launcher.h"
#include "nav.h"
#include "screen_wifi_scan.h"
#include "screen_wifi_connect.h"
#include "screen_storage.h"
#include "screen_audio.h"
#include "screen_settings.h"
#include "screen_assistant.h"
#include "../services/wifi_service.h"
#include "../services/display_service.h"

namespace launcher {

static lv_obj_t *s_wifi_status_label = nullptr;

static void refresh_wifi_status() {
    if (!s_wifi_status_label) return;
    if (wifi_service::is_connected()) {
        lv_label_set_text_fmt(s_wifi_status_label, LV_SYMBOL_WIFI " %s", wifi_service::get_ip().c_str());
        lv_obj_set_style_text_color(s_wifi_status_label, lv_color_hex(0xA6E3A1), LV_PART_MAIN);
    } else {
        lv_label_set_text(s_wifi_status_label, LV_SYMBOL_WIFI " Not connected");
        lv_obj_set_style_text_color(s_wifi_status_label, lv_color_hex(0xF38BA8), LV_PART_MAIN);
    }
}

// Runs inside esp_lvgl_port's own locked timer task — safe to touch LVGL objects directly here,
// same as any other lv_timer callback. Created once, in create(), and lives for the app's
// lifetime since the launcher screen itself is never destroyed (see nav.cpp).
static void wifi_status_timer_cb(lv_timer_t *timer) {
    (void)timer;
    refresh_wifi_status();
}

static void rotate_btn_cb(lv_event_t *e) {
    (void)e;
    display_service::toggle();
}

struct Tile {
    const char *icon;
    const char *label;
    lv_obj_t *(*create_screen)();
};

static const Tile TILES[] = {
    {LV_SYMBOL_WIFI, "WiFi Scan", screen_wifi_scan::create},
    {LV_SYMBOL_SETTINGS, "WiFi Connect", screen_wifi_connect::create},
    {LV_SYMBOL_SD_CARD, "Storage & Video", screen_storage::create},
    {LV_SYMBOL_VOLUME_MAX, "Audio", screen_audio::create},
    {LV_SYMBOL_EDIT, "Settings", screen_settings::create},
    {LV_SYMBOL_AUDIO, "AI Assistant", screen_assistant::create},
};

static void tile_click_cb(lv_event_t *e) {
    auto *create_fn = reinterpret_cast<lv_obj_t *(*)()>(lv_event_get_user_data(e));
    nav::push(create_fn());
}

static lv_obj_t *make_tile(lv_obj_t *parent, const Tile &tile) {
    lv_obj_t *btn = lv_btn_create(parent);
    lv_obj_set_size(btn, 200, 140);
    lv_obj_set_style_bg_color(btn, lv_color_hex(0x313244), LV_PART_MAIN);
    lv_obj_set_style_radius(btn, 16, LV_PART_MAIN);
    lv_obj_add_event_cb(btn, tile_click_cb, LV_EVENT_CLICKED, reinterpret_cast<void *>(tile.create_screen));

    lv_obj_t *icon = lv_label_create(btn);
    lv_label_set_text(icon, tile.icon);
    lv_obj_set_style_text_font(icon, &lv_font_montserrat_32, LV_PART_MAIN);
    lv_obj_set_style_text_color(icon, lv_color_hex(0x89B4FA), LV_PART_MAIN);
    lv_obj_align(icon, LV_ALIGN_TOP_MID, 0, 15);

    lv_obj_t *label = lv_label_create(btn);
    lv_label_set_text(label, tile.label);
    lv_obj_set_style_text_color(label, lv_color_hex(0xCDD6F4), LV_PART_MAIN);
    lv_obj_align(label, LV_ALIGN_BOTTOM_MID, 0, -15);

    return btn;
}

lv_obj_t *create() {
    // The whole screen is a flex column — title/subtitle, then the tile grid (which grows to
    // fill whatever's left), then a bottom status bar — instead of separately-guessed pixel
    // positions for each piece. That was the actual root cause of a UI bug found via on-hardware
    // testing this session (see docs/BRINGUP.md "Launcher header controls overlapped the
    // title"): fixed corner/percentage placements don't adapt to portrait vs. landscape without
    // separate math for each, and stacked drifted. Flex sizes itself correctly either way.
    lv_obj_t *scr = lv_obj_create(nullptr);
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x181825), LV_PART_MAIN);
    lv_obj_set_flex_flow(scr, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(scr, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_top(scr, 20, LV_PART_MAIN);
    lv_obj_set_style_pad_row(scr, 8, LV_PART_MAIN);

    lv_obj_t *title = lv_label_create(scr);
    lv_label_set_text(title, "CapabilityDemo");
    lv_obj_set_style_text_color(title, lv_color_hex(0xCDD6F4), LV_PART_MAIN);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_32, LV_PART_MAIN);

    lv_obj_t *sub = lv_label_create(scr);
    lv_label_set_text(sub, "JC4880P443C_I_W");
    lv_obj_set_style_text_color(sub, lv_color_hex(0x6c7086), LV_PART_MAIN);

    // Icon-only, pinned to the top-right corner regardless of the column flow above —
    // LV_OBJ_FLAG_IGNORE_LAYOUT excludes it from flex stacking so it floats independently. Icon-
    // only (no "Landscape"/"Portrait" text) specifically because a wide button crept into the
    // title before; a small square icon at the corner can't, by construction.
    lv_obj_t *rotate_btn = lv_btn_create(scr);
    lv_obj_add_flag(rotate_btn, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_set_size(rotate_btn, 44, 44);
    lv_obj_align(rotate_btn, LV_ALIGN_TOP_RIGHT, -10, 10);
    lv_obj_add_event_cb(rotate_btn, rotate_btn_cb, LV_EVENT_CLICKED, nullptr);
    lv_obj_t *rotate_icon = lv_label_create(rotate_btn);
    lv_label_set_text(rotate_icon, LV_SYMBOL_LOOP);
    lv_obj_center(rotate_icon);

    // flex_grow: fills whatever vertical space is left between the header above and the status
    // bar below, in either orientation, instead of a guessed percentage height.
    lv_obj_t *grid = lv_obj_create(scr);
    lv_obj_remove_style_all(grid);
    lv_obj_set_width(grid, LV_PCT(94));
    lv_obj_set_flex_grow(grid, 1);
    lv_obj_set_flex_flow(grid, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(grid, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    for (const auto &tile : TILES) {
        make_tile(grid, tile);
    }

    // Bottom status bar — reserved for WiFi plus any future status indicators, pinned near the
    // screen's bottom edge in either orientation.
    lv_obj_t *status_bar = lv_obj_create(scr);
    lv_obj_remove_style_all(status_bar);
    lv_obj_set_size(status_bar, LV_PCT(100), 26);
    lv_obj_set_style_pad_bottom(status_bar, 6, LV_PART_MAIN);
    lv_obj_set_flex_flow(status_bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(status_bar, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    s_wifi_status_label = lv_label_create(status_bar);
    lv_obj_set_style_text_font(s_wifi_status_label, &lv_font_montserrat_14, LV_PART_MAIN);
    refresh_wifi_status();
    lv_timer_create(wifi_status_timer_cb, 2000, nullptr);

    return scr;
}

} // namespace launcher
