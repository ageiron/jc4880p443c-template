#include "screen_wifi_connect.h"
#include "nav.h"
#include "keyboard_dialog.h"
#include "../services/wifi_service.h"
#include "../services/nvs_config.h"
#include "esp_lvgl_port.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string>

namespace screen_wifi_connect {

static lv_obj_t *s_status_label = nullptr;

struct ConnectArgs {
    std::string ssid;
    std::string password;
};

static void connect_task(void *arg) {
    auto *args = static_cast<ConnectArgs *>(arg);
    esp_err_t err = wifi_service::connect(args->ssid, args->password);
    if (lvgl_port_lock(0)) {
        if (s_status_label) {
            if (err == ESP_OK) {
                nvs_config::set_wifi_credentials(args->ssid, args->password);
                lv_label_set_text_fmt(s_status_label, "Connected to %s. IP: %s", args->ssid.c_str(),
                                        wifi_service::get_ip().c_str());
            } else {
                lv_label_set_text_fmt(s_status_label, "Connection failed: %s", wifi_service::get_last_fail_reason().c_str());
            }
        }
        lvgl_port_unlock();
    }
    delete args;
    vTaskDelete(nullptr);
}

static void start_connect(const std::string &ssid, const std::string &pass) {
    if (ssid.empty()) return;
    if (s_status_label) lv_label_set_text_fmt(s_status_label, "Connecting to %s...", ssid.c_str());
    auto *args = new ConnectArgs{ssid, pass};
    xTaskCreate(connect_task, "wifi_connect", 4096, args, 5, nullptr);
}

// Reconnects using whatever's already saved in NVS — the button the status text actually
// promises. The original version only offered "enter new details," with no way to just
// reconnect to the already-saved network without retyping the password — found via user
// testing on hardware.
static void reconnect_btn_cb(lv_event_t *e) {
    (void)e;
    std::string ssid, pass;
    if (nvs_config::get_wifi_credentials(ssid, pass) == ESP_OK) {
        start_connect(ssid, pass);
    }
}

static void change_network_btn_cb(lv_event_t *e) {
    (void)e;
    keyboard_dialog::show_two_field(lv_scr_act(), "WiFi network", "SSID", "Password",
        [](const std::string &ssid, const std::string &pass) { start_connect(ssid, pass); });
}

lv_obj_t *create() {
    lv_obj_t *scr = lv_obj_create(nullptr);
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x181825), LV_PART_MAIN);
    nav::add_back_button(scr);

    lv_obj_t *title = lv_label_create(scr);
    lv_label_set_text(title, "WiFi Connect");
    lv_obj_set_style_text_color(title, lv_color_hex(0xCDD6F4), LV_PART_MAIN);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_32, LV_PART_MAIN);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 65);

    lv_obj_t *note = lv_label_create(scr);
    lv_label_set_text(note, "Needed for the AI assistant screen to reach the internet.");
    lv_obj_set_style_text_color(note, lv_color_hex(0x6c7086), LV_PART_MAIN);
    lv_obj_align_to(note, title, LV_ALIGN_OUT_BOTTOM_MID, 0, 8);

    s_status_label = lv_label_create(scr);
    lv_label_set_long_mode(s_status_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(s_status_label, LV_PCT(80));
    lv_obj_set_style_text_align(s_status_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_color(s_status_label, lv_color_hex(0xA6E3A1), LV_PART_MAIN);
    lv_obj_align(s_status_label, LV_ALIGN_CENTER, 0, -50);

    std::string ssid, pass;
    bool has_saved = nvs_config::get_wifi_credentials(ssid, pass) == ESP_OK;
    if (has_saved) {
        lv_label_set_text_fmt(s_status_label, "Saved network: %s%s", ssid.c_str(),
                                wifi_service::is_connected() ? " (connected)" : " (tap Reconnect below)");
    } else {
        lv_label_set_text(s_status_label, "No network saved yet.");
    }

    if (has_saved) {
        lv_obj_t *reconnect_btn = lv_btn_create(scr);
        lv_obj_set_size(reconnect_btn, 240, 65);
        lv_obj_align(reconnect_btn, LV_ALIGN_CENTER, 0, 40);
        lv_obj_add_event_cb(reconnect_btn, reconnect_btn_cb, LV_EVENT_CLICKED, nullptr);
        lv_obj_t *reconnect_lbl = lv_label_create(reconnect_btn);
        lv_label_set_text(reconnect_lbl, "Reconnect");
        lv_obj_center(reconnect_lbl);
    }

    lv_obj_t *change_btn = lv_btn_create(scr);
    lv_obj_set_size(change_btn, 240, has_saved ? 55 : 70);
    lv_obj_align(change_btn, LV_ALIGN_CENTER, 0, has_saved ? 120 : 40);
    lv_obj_add_event_cb(change_btn, change_network_btn_cb, LV_EVENT_CLICKED, nullptr);
    lv_obj_t *change_lbl = lv_label_create(change_btn);
    lv_label_set_text(change_lbl, has_saved ? "Change Network" : "Enter WiFi Details");
    lv_obj_center(change_lbl);

    return scr;
}

} // namespace screen_wifi_connect
