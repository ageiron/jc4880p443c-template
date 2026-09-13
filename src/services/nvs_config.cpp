#include "nvs_config.h"
#include "nvs.h"
#include "esp_log.h"
#include <vector>

namespace nvs_config {

static const char *TAG = "nvs_config";
static const char *NAMESPACE = "capdemo";

static esp_err_t get_string(const char *key, std::string &out) {
    nvs_handle_t h;
    esp_err_t err = nvs_open(NAMESPACE, NVS_READONLY, &h);
    if (err != ESP_OK) return err;
    size_t len = 0;
    err = nvs_get_str(h, key, nullptr, &len);
    if (err != ESP_OK) {
        nvs_close(h);
        return err;
    }
    std::vector<char> buf(len);
    err = nvs_get_str(h, key, buf.data(), &len);
    nvs_close(h);
    if (err == ESP_OK) {
        out.assign(buf.data());
    }
    return err;
}

static esp_err_t set_string(const char *key, const std::string &value) {
    nvs_handle_t h;
    esp_err_t err = nvs_open(NAMESPACE, NVS_READWRITE, &h);
    if (err != ESP_OK) return err;
    err = nvs_set_str(h, key, value.c_str());
    if (err == ESP_OK) {
        err = nvs_commit(h);
    }
    nvs_close(h);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to save %s: %s", key, esp_err_to_name(err));
    }
    return err;
}

static bool has_key(const char *key) {
    std::string tmp;
    return get_string(key, tmp) == ESP_OK && !tmp.empty();
}

bool has_wifi_credentials() { return has_key("wifi_ssid"); }

esp_err_t set_wifi_credentials(const std::string &ssid, const std::string &password) {
    esp_err_t err = set_string("wifi_ssid", ssid);
    if (err != ESP_OK) return err;
    return set_string("wifi_pass", password);
}

esp_err_t get_wifi_credentials(std::string &ssid, std::string &password) {
    esp_err_t err = get_string("wifi_ssid", ssid);
    if (err != ESP_OK) return err;
    return get_string("wifi_pass", password);
}

esp_err_t clear_wifi_credentials() {
    nvs_handle_t h;
    esp_err_t err = nvs_open(NAMESPACE, NVS_READWRITE, &h);
    if (err != ESP_OK) return err;
    nvs_erase_key(h, "wifi_ssid");
    nvs_erase_key(h, "wifi_pass");
    err = nvs_commit(h);
    nvs_close(h);
    return err;
}

bool has_anthropic_key() { return has_key("anthropic_key"); }
esp_err_t set_anthropic_key(const std::string &key) { return set_string("anthropic_key", key); }
esp_err_t get_anthropic_key(std::string &key) { return get_string("anthropic_key", key); }

bool has_openai_key() { return has_key("openai_key"); }
esp_err_t set_openai_key(const std::string &key) { return set_string("openai_key", key); }
esp_err_t get_openai_key(std::string &key) { return get_string("openai_key", key); }

std::string mask_secret(const std::string &secret) {
    if (secret.size() <= 8) return "****";
    return secret.substr(0, 3) + "..." + secret.substr(secret.size() - 4);
}

bool get_use_local_llm() {
    std::string v;
    return get_string("use_local_llm", v) == ESP_OK && v == "1";
}
esp_err_t set_use_local_llm(bool enabled) { return set_string("use_local_llm", enabled ? "1" : "0"); }

bool has_local_llm_url() { return has_key("local_llm_url"); }
esp_err_t set_local_llm_url(const std::string &url) { return set_string("local_llm_url", url); }
esp_err_t get_local_llm_url(std::string &url) { return get_string("local_llm_url", url); }

esp_err_t set_local_llm_model(const std::string &model) { return set_string("local_llm_model", model); }
esp_err_t get_local_llm_model(std::string &model) {
    esp_err_t err = get_string("local_llm_model", model);
    if (err != ESP_OK || model.empty()) model = "local-model";
    return ESP_OK;
}

} // namespace nvs_config
