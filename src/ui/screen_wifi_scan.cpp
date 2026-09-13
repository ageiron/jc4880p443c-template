#include "screen_wifi_scan.h"
#include "nav.h"
#include "../services/wifi_service.h"
#include "esp_lvgl_port.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <cstdio>
#include <vector>

namespace screen_wifi_scan {

static lv_obj_t *s_list = nullptr;

static const char *auth_str(wifi_auth_mode_t m) {
    switch (m) {
        case WIFI_AUTH_OPEN: return "open";
        case WIFI_AUTH_WEP: return "WEP";
        case WIFI_AUTH_WPA_PSK: return "WPA";
        case WIFI_AUTH_WPA2_PSK: return "WPA2";
        case WIFI_AUTH_WPA_WPA2_PSK: return "WPA/WPA2";
        case WIFI_AUTH_WPA3_PSK: return "WPA3";
        case WIFI_AUTH_WPA2_WPA3_PSK: return "WPA2/WPA3";
        default: return "secured";
    }
}

static void scan_task(void *arg) {
    (void)arg;
    std::vector<wifi_service::ApInfo> results;
    esp_err_t err = wifi_service::scan(results);
    if (lvgl_port_lock(0)) {
        lv_obj_clean(s_list);
        if (err != ESP_OK) {
            lv_list_add_text(s_list, "Scan failed — check WiFi bring-up in docs/BRINGUP.md.");
        } else if (results.empty()) {
            lv_list_add_text(s_list, "No networks found.");
        } else {
            for (auto &ap : results) {
                char buf[96];
                snprintf(buf, sizeof(buf), "%s  (%d dBm, %s)", ap.ssid.empty() ? "[hidden]" : ap.ssid.c_str(),
                          ap.rssi, auth_str(ap.authmode));
                lv_list_add_text(s_list, buf);
            }
        }
        lvgl_port_unlock();
    }
    vTaskDelete(nullptr);
}

static void rescan_btn_cb(lv_event_t *e) {
    (void)e;
    lv_obj_clean(s_list);
    lv_list_add_text(s_list, "Scanning...");
    xTaskCreate(scan_task, "wifi_scan", 4096, nullptr, 5, nullptr);
}

lv_obj_t *create() {
    lv_obj_t *scr = lv_obj_create(nullptr);
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x181825), LV_PART_MAIN);
    nav::add_back_button(scr);

    lv_obj_t *title = lv_label_create(scr);
    lv_label_set_text(title, "WiFi Scan");
    lv_obj_set_style_text_color(title, lv_color_hex(0xCDD6F4), LV_PART_MAIN);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_32, LV_PART_MAIN);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 30);

    lv_obj_t *note = lv_label_create(scr);
    lv_label_set_text(note, "Read-only. Use WiFi Connect to actually join a network.");
    lv_obj_set_style_text_color(note, lv_color_hex(0x6c7086), LV_PART_MAIN);
    lv_obj_align_to(note, title, LV_ALIGN_OUT_BOTTOM_MID, 0, 8);

    s_list = lv_list_create(scr);
    lv_obj_set_size(s_list, LV_PCT(90), 520);
    lv_obj_align(s_list, LV_ALIGN_TOP_MID, 0, 110);
    lv_list_add_text(s_list, "Tap Scan to search for networks.");

    lv_obj_t *btn = lv_btn_create(scr);
    lv_obj_set_size(btn, 180, 60);
    lv_obj_align(btn, LV_ALIGN_BOTTOM_MID, 0, -20);
    lv_obj_add_event_cb(btn, rescan_btn_cb, LV_EVENT_CLICKED, nullptr);
    lv_obj_t *btn_lbl = lv_label_create(btn);
    lv_label_set_text(btn_lbl, "Scan");
    lv_obj_center(btn_lbl);

    return scr;
}

} // namespace screen_wifi_scan
