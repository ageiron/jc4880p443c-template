#include "serial_console.h"
#include "nvs_config.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <cstdio>
#include <cstring>
#include <string>

namespace serial_console {

static const char *TAG = "serial_console";
static constexpr int RX_BUF_SIZE = 4096; // API keys can run 100+ chars; give plenty of headroom

static std::string trim(const std::string &s) {
    size_t start = s.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    size_t end = s.find_last_not_of(" \t\r\n");
    return s.substr(start, end - start + 1);
}

static bool starts_with(const std::string &s, const char *prefix) {
    size_t len = strlen(prefix);
    return s.size() >= len && s.compare(0, len, prefix) == 0;
}

static void handle_line(const std::string &raw_line) {
    std::string line = trim(raw_line);
    if (line.empty()) return;

    const char *ANTHROPIC_PREFIX = "SET_ANTHROPIC_KEY:";
    const char *OPENAI_PREFIX = "SET_OPENAI_KEY:";

    const char *which = nullptr;
    esp_err_t (*setter)(const std::string &) = nullptr;

    if (starts_with(line, ANTHROPIC_PREFIX)) {
        which = "Anthropic";
        setter = nvs_config::set_anthropic_key;
        line = line.substr(strlen(ANTHROPIC_PREFIX));
    } else if (starts_with(line, OPENAI_PREFIX)) {
        which = "OpenAI";
        setter = nvs_config::set_openai_key;
        line = line.substr(strlen(OPENAI_PREFIX));
    } else {
        printf("ERROR: unrecognized command. Use SET_ANTHROPIC_KEY:<key> or SET_OPENAI_KEY:<key>\n");
        return;
    }

    std::string key = trim(line);
    if (key.empty()) {
        printf("ERROR: %s key was empty\n", which);
        return;
    }

    esp_err_t err = setter(key);
    if (err == ESP_OK) {
        printf("OK, saved %s key to NVS (%u chars)\n", which, (unsigned)key.size());
        ESP_LOGI(TAG, "%s key updated via serial console (%u chars)", which, (unsigned)key.size());
    } else {
        printf("ERROR: failed to save %s key: %s\n", which, esp_err_to_name(err));
    }
}

static void console_task(void *arg) {
    (void)arg;
    std::string line;
    printf("\nserial_console ready. Commands:\n"
           "  SET_ANTHROPIC_KEY:<key>\n"
           "  SET_OPENAI_KEY:<key>\n");

    // This board's console runs over USB-Serial/JTAG, not a physical UART (see
    // sdkconfig.defaults: CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=y) — installing our own UART0
    // driver on top of that crashed the board in an instant boot loop (wrong peripheral
    // entirely; found via serial capture on hardware). ESP-IDF already wires that console
    // peripheral to stdin/stdout, so plain blocking stdio reads are the correct, driver-free way
    // to read from the same USB connection the ESP_LOG output already uses.
    for (;;) {
        int c = getchar();
        if (c == EOF) {
            vTaskDelay(pdMS_TO_TICKS(20));
            continue;
        }
        if (c == '\n' || c == '\r') {
            if (!line.empty()) {
                handle_line(line);
                line.clear();
            }
            continue;
        }
        line.push_back(static_cast<char>(c));
        if (line.size() > RX_BUF_SIZE) {
            printf("ERROR: line too long, discarding\n");
            line.clear();
        }
    }
}

void start() {
    xTaskCreate(console_task, "serial_console", 4096, NULL, 5, NULL);
}

} // namespace serial_console
