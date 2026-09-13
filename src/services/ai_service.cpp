#include "ai_service.h"
#include "audio_service.h"
#include "wifi_service.h"
#include "nvs_config.h"
#include "http_client.h"
#include "cJSON.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <atomic>
#include <cstring>
#include <vector>

namespace ai_service {

static const char *TAG = "ai_service";
static std::atomic<bool> s_stop_requested{false};
static std::atomic<bool> s_turn_active{false};

Prerequisites check_prerequisites() {
    Prerequisites p{};
    p.wifi_ok = wifi_service::is_connected();
    p.using_local_llm = nvs_config::get_use_local_llm();
    p.reply_backend_ok = p.using_local_llm ? nvs_config::has_local_llm_url() : nvs_config::has_anthropic_key();
    p.openai_key_ok = nvs_config::has_openai_key();
    return p;
}

void stop_recording() { s_stop_requested = true; }

// ---- WAV wrapper (Whisper needs a real WAV, not raw PCM) ------------------

#pragma pack(push, 1)
struct WavHeader {
    char riff[4] = {'R', 'I', 'F', 'F'};
    uint32_t chunk_size = 0;
    char wave[4] = {'W', 'A', 'V', 'E'};
    char fmt[4] = {'f', 'm', 't', ' '};
    uint32_t fmt_size = 16;
    uint16_t audio_format = 1; // PCM
    uint16_t num_channels = 1;
    uint32_t sample_rate = 0;
    uint32_t byte_rate = 0;
    uint16_t block_align = 0;
    uint16_t bits_per_sample = 16;
    char data[4] = {'d', 'a', 't', 'a'};
    uint32_t data_size = 0;
};
#pragma pack(pop)

// Returned buffer lives in PSRAM, not a std::vector — the recorded audio can run into the
// hundreds of KB and this project's internal SRAM heap is only ~250KB total. A plain
// std::vector construction here previously threw std::bad_alloc, which — since this project
// builds without C++ exception support — immediately called abort(). See PsramBuf's comment in
// http_client.h and docs/BRINGUP.md.
static http_client::PsramBuf wrap_wav(const int16_t *pcm, size_t num_samples, uint32_t sample_rate) {
    WavHeader hdr;
    hdr.sample_rate = sample_rate;
    hdr.block_align = static_cast<uint16_t>(hdr.num_channels * hdr.bits_per_sample / 8);
    hdr.byte_rate = sample_rate * hdr.block_align;
    hdr.data_size = static_cast<uint32_t>(num_samples * sizeof(int16_t));
    hdr.chunk_size = 36 + hdr.data_size;

    http_client::PsramBuf out;
    out.reserve(sizeof(WavHeader) + hdr.data_size);
    out.append(&hdr, sizeof(WavHeader));
    out.append(pcm, hdr.data_size);
    return out;
}

// ---- Pipeline steps ---------------------------------------------------------

static bool whisper_transcribe(const http_client::PsramBuf &wav, const std::string &api_key, std::string &text_out) {
    std::vector<http_client::Header> headers = {{"Authorization", "Bearer " + api_key}};
    // Force English — without this, Whisper auto-detects the spoken language, and a short or
    // noisy recording can easily be mis-detected as something else (found via on-hardware
    // testing: a recording came back transcribed as Spanish, and Claude naturally replied in
    // kind since it just answers in whatever language its input is).
    std::vector<std::pair<std::string, std::string>> fields = {{"model", "whisper-1"}, {"language", "en"}};
    std::string response;
    int status = 0;
    esp_err_t err = http_client::post_multipart_file("https://api.openai.com/v1/audio/transcriptions", headers,
                                                       "file", "audio.wav", "audio/wav", wav.data, wav.size,
                                                       fields, response, &status);
    if (err != ESP_OK || status / 100 != 2) {
        ESP_LOGE(TAG, "Whisper failed: err=%s status=%d", esp_err_to_name(err), status);
        return false;
    }
    cJSON *root = cJSON_Parse(response.c_str());
    if (!root) return false;
    cJSON *text = cJSON_GetObjectItem(root, "text");
    bool ok = cJSON_IsString(text);
    if (ok) text_out = text->valuestring;
    cJSON_Delete(root);
    return ok;
}

// Talks to a local LM Studio/Ollama server on the LAN instead of the Anthropic API, via its
// OpenAI-compatible /v1/chat/completions endpoint (both LM Studio and modern Ollama support
// this exact shape, so one code path covers both). No API key — LAN-only, no auth by default.
static bool local_llm_reply(const std::string &user_text, const std::string &base_url, const std::string &model,
                             std::string &reply_out) {
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "model", model.c_str());
    cJSON_AddNumberToObject(root, "max_tokens", 512);
    cJSON *messages = cJSON_AddArrayToObject(root, "messages");
    cJSON *msg = cJSON_CreateObject();
    cJSON_AddStringToObject(msg, "role", "user");
    cJSON_AddStringToObject(msg, "content", user_text.c_str());
    cJSON_AddItemToArray(messages, msg);
    char *body_cstr = cJSON_PrintUnformatted(root);
    std::string body(body_cstr);
    cJSON_free(body_cstr);
    cJSON_Delete(root);

    std::string url = base_url;
    while (!url.empty() && url.back() == '/') url.pop_back();
    url += "/v1/chat/completions";

    std::vector<http_client::Header> headers; // no auth — LAN-only server
    std::string response;
    int status = 0;
    esp_err_t err = http_client::post_json(url, headers, body, response, &status);
    if (err != ESP_OK || status / 100 != 2) {
        ESP_LOGE(TAG, "Local LLM request failed: err=%s status=%d (url=%s)", esp_err_to_name(err), status,
                  url.c_str());
        return false;
    }
    cJSON *resp_root = cJSON_Parse(response.c_str());
    if (!resp_root) return false;
    bool ok = false;
    cJSON *choices = cJSON_GetObjectItem(resp_root, "choices");
    if (cJSON_IsArray(choices) && cJSON_GetArraySize(choices) > 0) {
        cJSON *first = cJSON_GetArrayItem(choices, 0);
        cJSON *message = cJSON_GetObjectItem(first, "message");
        cJSON *content = message ? cJSON_GetObjectItem(message, "content") : nullptr;
        if (cJSON_IsString(content)) {
            reply_out = content->valuestring;
            ok = true;
        }
    }
    cJSON_Delete(resp_root);
    return ok;
}

static bool claude_reply(const std::string &user_text, const std::string &api_key, std::string &reply_out) {
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "model", "claude-sonnet-5");
    cJSON_AddNumberToObject(root, "max_tokens", 512);
    cJSON *messages = cJSON_AddArrayToObject(root, "messages");
    cJSON *msg = cJSON_CreateObject();
    cJSON_AddStringToObject(msg, "role", "user");
    cJSON_AddStringToObject(msg, "content", user_text.c_str());
    cJSON_AddItemToArray(messages, msg);
    char *body_cstr = cJSON_PrintUnformatted(root);
    std::string body(body_cstr);
    cJSON_free(body_cstr);
    cJSON_Delete(root);

    std::vector<http_client::Header> headers = {
        {"x-api-key", api_key},
        {"anthropic-version", "2023-06-01"},
    };
    std::string response;
    int status = 0;
    esp_err_t err = http_client::post_json("https://api.anthropic.com/v1/messages", headers, body, response, &status);
    if (err != ESP_OK || status / 100 != 2) {
        ESP_LOGE(TAG, "Claude request failed: err=%s status=%d", esp_err_to_name(err), status);
        return false;
    }
    cJSON *resp_root = cJSON_Parse(response.c_str());
    if (!resp_root) return false;
    cJSON *content = cJSON_GetObjectItem(resp_root, "content");
    bool ok = false;
    if (cJSON_IsArray(content) && cJSON_GetArraySize(content) > 0) {
        cJSON *first = cJSON_GetArrayItem(content, 0);
        cJSON *text = cJSON_GetObjectItem(first, "text");
        if (cJSON_IsString(text)) {
            reply_out = text->valuestring;
            ok = true;
        }
    }
    cJSON_Delete(resp_root);
    return ok;
}

static bool openai_tts(const std::string &text, const std::string &api_key, http_client::PsramBuf &pcm_out) {
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "model", "tts-1");
    cJSON_AddStringToObject(root, "input", text.c_str());
    cJSON_AddStringToObject(root, "voice", "alloy");
    cJSON_AddStringToObject(root, "response_format", "pcm"); // raw PCM: no MP3/Opus decoder needed on-device
    char *body_cstr = cJSON_PrintUnformatted(root);
    std::string body(body_cstr);
    cJSON_free(body_cstr);
    cJSON_Delete(root);

    std::vector<http_client::Header> headers = {{"Authorization", "Bearer " + api_key}};
    int status = 0;
    esp_err_t err =
        http_client::post_json_binary_response("https://api.openai.com/v1/audio/speech", headers, body, pcm_out, &status);
    if (err != ESP_OK || status / 100 != 2) {
        ESP_LOGE(TAG, "TTS request failed: err=%s status=%d", esp_err_to_name(err), status);
        return false;
    }
    return true;
}

// ---- Turn orchestration -------------------------------------------------------

struct TurnCtx {
    uint32_t max_seconds;
    StateCb on_state;
    TranscriptCb on_transcript;
};

static void run_turn(TurnCtx *ctx) {
    Prerequisites pre = check_prerequisites();
    if (!pre.wifi_ok) {
        ctx->on_state(State::Error, "Not connected to WiFi — use the WiFi Connect screen first.");
        return;
    }
    if (!pre.openai_key_ok) {
        ctx->on_state(State::Error, "Missing OpenAI API key — set it in Settings first.");
        return;
    }
    if (!pre.reply_backend_ok) {
        ctx->on_state(State::Error, pre.using_local_llm
                                         ? "Local LLM URL not set — set it in Settings first."
                                         : "Missing Anthropic API key — set it in Settings first.");
        return;
    }

    ctx->on_state(State::Recording, "");
    const uint32_t sample_rate = 16000;
    const size_t max_samples = sample_rate * ctx->max_seconds;
    auto *pcm = static_cast<int16_t *>(heap_caps_malloc(max_samples * sizeof(int16_t), MALLOC_CAP_SPIRAM));
    if (!pcm) {
        ctx->on_state(State::Error, "Out of memory recording audio.");
        return;
    }

    size_t total_samples = 0;
    audio_service::start_capture();
    const size_t chunk_samples = 512;
    while (total_samples + chunk_samples <= max_samples && !s_stop_requested) {
        size_t bytes_read = 0;
        esp_err_t err = audio_service::read_capture(reinterpret_cast<uint8_t *>(pcm + total_samples),
                                                      chunk_samples * sizeof(int16_t), &bytes_read);
        if (err != ESP_OK) break;
        total_samples += bytes_read / sizeof(int16_t);
    }
    audio_service::stop_capture();

    if (total_samples < sample_rate / 2) { // less than ~0.5s captured
        heap_caps_free(pcm);
        ctx->on_state(State::Error, "Recording too short — hold the button while you talk.");
        return;
    }

    ctx->on_state(State::Transcribing, "");
    http_client::PsramBuf wav = wrap_wav(pcm, total_samples, sample_rate);
    heap_caps_free(pcm);

    std::string openai_key, anthropic_key;
    nvs_config::get_openai_key(openai_key);
    nvs_config::get_anthropic_key(anthropic_key);

    std::string user_text;
    if (!whisper_transcribe(wav, openai_key, user_text) || user_text.empty()) {
        ctx->on_state(State::Error, "Couldn't transcribe that — check the OpenAI key or try again.");
        return;
    }

    ctx->on_state(State::Thinking, user_text);
    std::string reply_text;
    bool reply_ok;
    if (pre.using_local_llm) {
        std::string base_url, model;
        nvs_config::get_local_llm_url(base_url);
        nvs_config::get_local_llm_model(model);
        reply_ok = local_llm_reply(user_text, base_url, model, reply_text);
        if (!reply_ok) {
            ctx->on_state(State::Error, "Local LLM request failed — check the URL and that it's running.");
        }
    } else {
        reply_ok = claude_reply(user_text, anthropic_key, reply_text);
        if (!reply_ok) {
            ctx->on_state(State::Error, "Claude request failed — check the Anthropic key or try again.");
        }
    }
    if (!reply_ok) return;

    ctx->on_transcript(user_text, reply_text);

    ctx->on_state(State::Speaking, reply_text);
    http_client::PsramBuf tts_pcm;
    if (openai_tts(reply_text, openai_key, tts_pcm) && tts_pcm.size > 0) {
        audio_service::play_pcm(tts_pcm.data, tts_pcm.size, 24000); // OpenAI TTS pcm format is 24kHz
    }

    ctx->on_state(State::Idle, "");
}

static void turn_task(void *arg) {
    auto *ctx = static_cast<TurnCtx *>(arg);
    s_stop_requested = false;
    run_turn(ctx);
    s_turn_active = false;
    delete ctx;
    vTaskDelete(nullptr);
}

void start_turn(uint32_t max_seconds, StateCb on_state, TranscriptCb on_transcript) {
    if (s_turn_active) return;
    s_turn_active = true;
    auto *ctx = new TurnCtx{max_seconds, std::move(on_state), std::move(on_transcript)};
    xTaskCreate(turn_task, "ai_turn", 8192, ctx, 5, nullptr);
}

} // namespace ai_service
