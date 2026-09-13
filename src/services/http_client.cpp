#include "http_client.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "esp_log.h"

namespace http_client {

static const char *TAG = "http_client";

// Accumulates response bytes for every caller into PSRAM — a response can be a large TTS PCM
// payload (hundreds of KB), and this project's internal SRAM heap is only ~250KB total (see
// PsramBuf's comment in http_client.h). Text/JSON callers copy the (small) final result into a
// std::string once, after the download completes.
static esp_err_t event_handler(esp_http_client_event_t *evt) {
    if (evt->event_id == HTTP_EVENT_ON_DATA) {
        auto *acc = static_cast<PsramBuf *>(evt->user_data);
        if (acc && evt->data_len > 0) {
            acc->append(evt->data, evt->data_len);
        }
    }
    return ESP_OK;
}

static esp_err_t perform_post(const std::string &url, const std::vector<Header> &headers,
                               const char *post_data, size_t post_len, const char *content_type_override,
                               PsramBuf &response, int *http_status) {
    response.free_buf();

    esp_http_client_config_t config = {};
    config.url = url.c_str();
    config.event_handler = event_handler;
    config.user_data = &response;
    config.crt_bundle_attach = esp_crt_bundle_attach;
    config.timeout_ms = 30000;
    config.buffer_size = 2048;

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) return ESP_FAIL;

    esp_http_client_set_method(client, HTTP_METHOD_POST);
    for (const auto &h : headers) {
        esp_http_client_set_header(client, h.name.c_str(), h.value.c_str());
    }
    if (content_type_override) {
        esp_http_client_set_header(client, "Content-Type", content_type_override);
    }
    esp_http_client_set_post_field(client, post_data, post_len);

    esp_err_t err = esp_http_client_perform(client);
    if (http_status) {
        *http_status = (err == ESP_OK) ? esp_http_client_get_status_code(client) : 0;
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "HTTP request to %s failed: %s", url.c_str(), esp_err_to_name(err));
    }
    esp_http_client_cleanup(client);
    return err;
}

esp_err_t post_json(const std::string &url, const std::vector<Header> &headers, const std::string &body,
                     std::string &response_body, int *http_status) {
    PsramBuf response;
    esp_err_t err = perform_post(url, headers, body.c_str(), body.size(), "application/json", response, http_status);
    if (response.size > 0) response_body.assign(reinterpret_cast<const char *>(response.data), response.size);
    else response_body.clear();
    return err;
}

esp_err_t post_json_binary_response(const std::string &url, const std::vector<Header> &headers,
                                     const std::string &body, PsramBuf &response_bytes,
                                     int *http_status) {
    return perform_post(url, headers, body.c_str(), body.size(), "application/json", response_bytes, http_status);
}

esp_err_t post_multipart_file(const std::string &url, const std::vector<Header> &headers,
                               const std::string &field_name, const std::string &filename,
                               const std::string &content_type, const uint8_t *file_data, size_t file_len,
                               const std::vector<std::pair<std::string, std::string>> &extra_fields,
                               std::string &response_body, int *http_status) {
    static const char *BOUNDARY = "----CapabilityDemoBoundary7f3a1c";

    // Built in PSRAM, not std::string — this embeds the full WAV file (can run into the hundreds
    // of KB) and this project's internal SRAM heap is only ~250KB total. See PsramBuf's comment.
    PsramBuf body;
    body.reserve(file_len + 512);

    for (const auto &kv : extra_fields) {
        body.append(std::string("--") + BOUNDARY);
        body.append("\r\nContent-Disposition: form-data; name=\"");
        body.append(kv.first);
        body.append("\"\r\n\r\n");
        body.append(kv.second);
        body.append("\r\n");
    }

    body.append(std::string("--") + BOUNDARY);
    body.append("\r\nContent-Disposition: form-data; name=\"");
    body.append(field_name);
    body.append("\"; filename=\"");
    body.append(filename);
    body.append("\"\r\nContent-Type: ");
    body.append(content_type);
    body.append("\r\n\r\n");
    body.append(file_data, file_len);
    body.append(std::string("\r\n--") + BOUNDARY + "--\r\n");

    std::string content_type_header = std::string("multipart/form-data; boundary=") + BOUNDARY;
    PsramBuf response;
    esp_err_t err = perform_post(url, headers, reinterpret_cast<const char *>(body.data), body.size,
                                  content_type_header.c_str(), response, http_status);
    if (response.size > 0) response_body.assign(reinterpret_cast<const char *>(response.data), response.size);
    else response_body.clear();
    return err;
}

} // namespace http_client
