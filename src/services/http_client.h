#pragma once
// Thin esp_http_client + esp_crt_bundle wrapper: everything ai_service needs to talk to
// Anthropic/OpenAI over HTTPS (JSON POST, multipart file upload, binary response) lives here so
// TLS trust and auth-header handling are written once and carefully, not three times.
// See FUNCTIONAL_DESCRIPTION.md "Security / secrets" — never log a header value that might be
// an API key.
#include "esp_err.h"
#include "esp_heap_caps.h"
#include <cstdint>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

namespace http_client {

struct Header {
    std::string name;
    std::string value;
};

// Growable byte buffer allocated in PSRAM. Use this instead of std::string/std::vector<uint8_t>
// for anything that can reach into the hundreds of KB (recorded WAV audio, a multipart body that
// embeds it, a TTS PCM response) — this project's internal SRAM heap is only ~250KB total, and
// those default containers never reach into PSRAM here (CONFIG_SPIRAM_USE_CAPS_ALLOC, not
// CONFIG_SPIRAM_USE_MALLOC — see sdkconfig.defaults). Same class of bug as
// docs/BRINGUP.md "Copied JPEG input buffers must be allocated in PSRAM explicitly", just found
// in the AI assistant pipeline instead of the video one: a plain std::vector construction for the
// recorded WAV bytes threw std::bad_alloc, which — since this project builds without C++
// exception support — immediately called abort() instead of failing gracefully. Found via
// on-hardware testing + a symbolized crash backtrace, see docs/BRINGUP.md.
struct PsramBuf {
    uint8_t *data = nullptr;
    size_t size = 0;
    size_t capacity = 0;

    PsramBuf() = default;
    PsramBuf(const PsramBuf &) = delete;
    PsramBuf &operator=(const PsramBuf &) = delete;
    PsramBuf(PsramBuf &&other) noexcept { *this = std::move(other); }
    PsramBuf &operator=(PsramBuf &&other) noexcept {
        if (this != &other) {
            free_buf();
            data = other.data;
            size = other.size;
            capacity = other.capacity;
            other.data = nullptr;
            other.size = 0;
            other.capacity = 0;
        }
        return *this;
    }
    ~PsramBuf() { free_buf(); }

    void free_buf() {
        if (data) heap_caps_free(data);
        data = nullptr;
        size = 0;
        capacity = 0;
    }

    void reserve(size_t new_cap) {
        if (new_cap <= capacity) return;
        auto *p = static_cast<uint8_t *>(heap_caps_realloc(data, new_cap, MALLOC_CAP_SPIRAM));
        if (!p) return; // allocation failed — append() below will stop growing past `size`
        data = p;
        capacity = new_cap;
    }

    void append(const void *src, size_t len) {
        if (len == 0) return;
        if (size + len > capacity) {
            size_t new_cap = capacity ? capacity * 2 : 4096;
            while (new_cap < size + len) new_cap *= 2;
            reserve(new_cap);
        }
        if (size + len > capacity) return; // out of PSRAM; caller can compare size vs. expected
        memcpy(data + size, src, len);
        size += len;
    }
    void append(const std::string &s) { append(s.data(), s.size()); }
};

// JSON (or any text) POST. Fills response_body on any HTTP response (check *http_status).
esp_err_t post_json(const std::string &url, const std::vector<Header> &headers, const std::string &body,
                     std::string &response_body, int *http_status = nullptr);

// Same as post_json but keeps the response as raw bytes (e.g. TTS audio, not text) in PSRAM.
esp_err_t post_json_binary_response(const std::string &url, const std::vector<Header> &headers,
                                     const std::string &body, PsramBuf &response_bytes,
                                     int *http_status = nullptr);

// multipart/form-data POST with one file field plus optional plain fields (e.g. Whisper upload).
esp_err_t post_multipart_file(const std::string &url, const std::vector<Header> &headers,
                               const std::string &field_name, const std::string &filename,
                               const std::string &content_type, const uint8_t *file_data, size_t file_len,
                               const std::vector<std::pair<std::string, std::string>> &extra_fields,
                               std::string &response_body, int *http_status = nullptr);

} // namespace http_client
