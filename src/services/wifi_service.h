#pragma once
// Wraps the ESP-HOSTED-backed esp_wifi driver (WiFi runs on the ESP32-C6 co-processor over
// SDIO — see docs/BRINGUP.md "WiFi (ESP-HOSTED via ESP32-C6)"). The Kconfig/component wiring for
// that was already proven by the template; this is the first application-level code that
// actually calls esp_wifi_init()/connect() on top of it.
#include "esp_err.h"
#include "esp_wifi_types.h"
#include <string>
#include <vector>

namespace wifi_service {

struct ApInfo {
    std::string ssid;
    int8_t rssi;
    wifi_auth_mode_t authmode;
};

// Idempotent; brings up esp_netif + the default event loop + esp_wifi in STA mode. Called
// lazily by scan()/connect() if not already done — no need to call this explicitly.
esp_err_t init();

// Blocking scan (a few seconds). Read-only — does not join anything.
esp_err_t scan(std::vector<ApInfo> &results);

// Blocking connect/join, with a per-attempt timeout and an internal bounded retry (up to 4
// attempts total) for transient failures — see is_transient_reason() in wifi_service.cpp. Worst
// case (every attempt times out) is roughly 4 * timeout_ms; the 8s default keeps that under
// ~35s. Persist credentials yourself via nvs_config on success.
esp_err_t connect(const std::string &ssid, const std::string &password, uint32_t timeout_ms = 8000);

void disconnect();
bool is_connected();
std::string get_ip();

// Human-readable reason the most recent connect() attempt failed (or "no attempt made yet").
// Call after connect() returns an error — don't rely on it otherwise, it reflects whatever the
// last WIFI_EVENT_STA_DISCONNECTED carried, which could be from a prior attempt.
std::string get_last_fail_reason();

} // namespace wifi_service
