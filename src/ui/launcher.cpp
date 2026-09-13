#include "launcher.h"
#include "nav.h"
#include "screen_wifi_scan.h"
#include "screen_wifi_connect.h"
#include "screen_storage.h"
#include "screen_audio.h"
#include "screen_settings.h"
#include "screen_assistant.h"

namespace launcher {

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
    lv_obj_t *scr = lv_obj_create(nullptr);
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x181825), LV_PART_MAIN);

    lv_obj_t *title = lv_label_create(scr);
    lv_label_set_text(title, "CapabilityDemo");
    lv_obj_set_style_text_color(title, lv_color_hex(0xCDD6F4), LV_PART_MAIN);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_32, LV_PART_MAIN);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 20);

    lv_obj_t *sub = lv_label_create(scr);
    lv_label_set_text(sub, "JC4880P443C_I_W");
    lv_obj_set_style_text_color(sub, lv_color_hex(0x6c7086), LV_PART_MAIN);
    lv_obj_align_to(sub, title, LV_ALIGN_OUT_BOTTOM_MID, 0, 5);

    lv_obj_t *grid = lv_obj_create(scr);
    lv_obj_remove_style_all(grid);
    lv_obj_set_size(grid, LV_PCT(94), 600);
    lv_obj_align(grid, LV_ALIGN_TOP_MID, 0, 95);
    lv_obj_set_flex_flow(grid, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(grid, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    for (const auto &tile : TILES) {
        make_tile(grid, tile);
    }

    return scr;
}

} // namespace launcher
