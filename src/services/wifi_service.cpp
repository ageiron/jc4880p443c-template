#include "wifi_service.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_hosted.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include <cstring>

namespace wifi_service {

static const char *TAG = "wifi_service";
static EventGroupHandle_t s_event_group = nullptr;
static constexpr int CONNECTED_BIT = BIT0;
static constexpr int FAIL_BIT = BIT1;
static constexpr int STARTED_BIT = BIT2;
static bool s_inited = false;
static char s_ip[16] = "";
static uint8_t s_last_disconnect_reason = 0;

static void event_handler(void *arg, esp_event_base_t base, int32_t id, void *event_data) {
    (void)arg;
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        // esp_wifi_scan_start()/connect() over the ESP-HOSTED RPC link can race
        // esp_wifi_start() if called immediately after it returns — wait for this event
        // (see init() below) rather than assuming the STA is ready the instant start() returns.
        xEventGroupSetBits(s_event_group, STARTED_BIT);
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        s_ip[0] = '\0';
        auto *disc = static_cast<wifi_event_sta_disconnected_t *>(event_data);
        s_last_disconnect_reason = disc->reason;
        xEventGroupSetBits(s_event_group, FAIL_BIT);
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        auto *event = static_cast<ip_event_got_ip_t *>(event_data);
        snprintf(s_ip, sizeof(s_ip), IPSTR, IP2STR(&event->ip_info.ip));
        xEventGroupSetBits(s_event_group, CONNECTED_BIT);
    }
}

esp_err_t init() {
    if (s_inited) return ESP_OK;

    s_event_group = xEventGroupCreate();
    if (!s_event_group) return ESP_ERR_NO_MEM;

    esp_err_t err = esp_netif_init();
    if (err != ESP_OK) return err;

    err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        return err; // INVALID_STATE = already created elsewhere, fine
    }

    // ESP-HOSTED (P4<->C6 WiFi transport over SDIO) must be brought up explicitly before any
    // esp_wifi_* call — without this, esp_wifi_init() silently fails ("Transport not
    // initialized") and esp_netif_create_default_wifi_sta() crashes the app. See
    // docs/BRINGUP.md "WiFi (ESP-HOSTED via ESP32-C6)".
    int hosted_err = esp_hosted_init();
    if (hosted_err != 0) {
        ESP_LOGE(TAG, "esp_hosted_init failed: %d", hosted_err);
        return ESP_FAIL;
    }
    hosted_err = esp_hosted_connect_to_slave();
    if (hosted_err != 0) {
        ESP_LOGE(TAG, "esp_hosted_connect_to_slave failed: %d", hosted_err);
        return ESP_FAIL;
    }

    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    err = esp_wifi_init(&cfg);
    if (err != ESP_OK) return err;

    esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, nullptr);
    esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, nullptr);

    err = esp_wifi_set_mode(WIFI_MODE_STA);
    if (err != ESP_OK) return err;

    err = esp_wifi_start();
    if (err != ESP_OK) return err;

    // Don't return until the STA is actually ready — see the STARTED_BIT comment above.
    xEventGroupWaitBits(s_event_group, STARTED_BIT, pdFALSE, pdFALSE, pdMS_TO_TICKS(5000));

    s_inited = true;
    ESP_LOGI(TAG, "WiFi (ESP-HOSTED/C6) started in STA mode");
    return ESP_OK;
}

esp_err_t scan(std::vector<ApInfo> &results) {
    results.clear();
    if (!s_inited) {
        esp_err_t err = init();
        if (err != ESP_OK) return err;
    }

    wifi_scan_config_t scan_cfg = {};
    scan_cfg.show_hidden = true;

    // esp_wifi_scan_start() can return ESP_ERR_WIFI_STATE for ~1-2s after the STA-start event
    // has fired — measured on hardware: the C6 co-processor is still settling internally over
    // ESP-HOSTED's SDIO/RPC link (its own "disconnected/idle" event can arrive ~1s after the
    // host-side STA-start event does), and there's no host-visible event for "C6 is actually
    // ready to scan now". Retry with a generous total budget rather than a fixed short one. See
    // docs/BRINGUP.md "WiFi (ESP-HOSTED...)".
    static constexpr int MAX_ATTEMPTS = 10;
    esp_err_t err = ESP_FAIL;
    for (int attempt = 0; attempt < MAX_ATTEMPTS; attempt++) {
        err = esp_wifi_scan_start(&scan_cfg, true /* blocking */);
        if (err == ESP_OK) break;
        if (err != ESP_ERR_WIFI_STATE) break; // some other error — don't retry blindly
        ESP_LOGW(TAG, "scan_start: WiFi not settled yet, retrying (%d/%d)", attempt + 1, MAX_ATTEMPTS);
        vTaskDelay(pdMS_TO_TICKS(500));
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "scan_start failed: %s", esp_err_to_name(err));
        return err;
    }

    uint16_t num = 0;
    esp_wifi_scan_get_ap_num(&num);
    if (num == 0) return ESP_OK;

    std::vector<wifi_ap_record_t> records(num);
    err = esp_wifi_scan_get_ap_records(&num, records.data());
    if (err != ESP_OK) return err;

    results.reserve(num);
    for (uint16_t i = 0; i < num; i++) {
        ApInfo info;
        info.ssid = reinterpret_cast<const char *>(records[i].ssid);
        info.rssi = records[i].rssi;
        info.authmode = records[i].authmode;
        results.push_back(info);
    }
    return ESP_OK;
}

esp_err_t connect(const std::string &ssid, const std::string &password, uint32_t timeout_ms) {
    if (!s_inited) {
        esp_err_t err = init();
        if (err != ESP_OK) return err;
    }

    xEventGroupClearBits(s_event_group, CONNECTED_BIT | FAIL_BIT);

    wifi_config_t wifi_config = {};
    strncpy(reinterpret_cast<char *>(wifi_config.sta.ssid), ssid.c_str(), sizeof(wifi_config.sta.ssid) - 1);
    strncpy(reinterpret_cast<char *>(wifi_config.sta.password), password.c_str(), sizeof(wifi_config.sta.password) - 1);
    wifi_config.sta.threshold.authmode = password.empty() ? WIFI_AUTH_OPEN : WIFI_AUTH_WPA2_PSK;

    esp_err_t err = esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
    if (err != ESP_OK) return err;

    err = esp_wifi_connect();
    if (err != ESP_OK) {
        // Already-connected-elsewhere case: disconnect and retry once.
        esp_wifi_disconnect();
        err = esp_wifi_connect();
        if (err != ESP_OK) return err;
    }

    EventBits_t bits = xEventGroupWaitBits(s_event_group, CONNECTED_BIT | FAIL_BIT, pdFALSE, pdFALSE,
                                            pdMS_TO_TICKS(timeout_ms));
    if (bits & CONNECTED_BIT) {
        ESP_LOGI(TAG, "Connected to %s, IP %s", ssid.c_str(), s_ip);
        return ESP_OK;
    }
    ESP_LOGW(TAG, "Failed to connect to %s (reason %u: %s)", ssid.c_str(), s_last_disconnect_reason,
              get_last_fail_reason().c_str());
    return ESP_FAIL;
}

void disconnect() {
    esp_wifi_disconnect();
    s_ip[0] = '\0';
}

bool is_connected() {
    wifi_ap_record_t info;
    return esp_wifi_sta_get_ap_info(&info) == ESP_OK;
}

std::string get_ip() { return std::string(s_ip); }

std::string get_last_fail_reason() {
    // Reason codes from esp_wifi_types_generic.h (WIFI_REASON_*). Only the common,
    // actionable-for-a-user ones get a friendly string; anything else falls back to the raw
    // code so it's still diagnosable without guessing blindly.
    switch (s_last_disconnect_reason) {
        case 2:   return "authentication expired";
        case 15:  return "wrong password (auth failed)";
        case 14:  return "wrong password (MIC failure)";
        case 201: return "network not found";
        case 202: return "authentication failed";
        case 203: return "association failed — network may need a different security type";
        case 204: return "wrong password (handshake timeout)";
        case 205: return "connection timed out";
        default:
            if (s_last_disconnect_reason == 0) return "no attempt made yet";
            return "disconnected (reason code " + std::to_string(s_last_disconnect_reason) + ")";
    }
}

} // namespace wifi_service
