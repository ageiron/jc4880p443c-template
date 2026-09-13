#pragma once
// Typed NVS storage for the secrets this app needs: WiFi credentials and the two AI-assistant
// API keys. Namespace "capdemo". Never hardcode these in source — see FUNCTIONAL_DESCRIPTION.md
// "Security / secrets". nvs_flash_init() must already have run (it does, in app_main()) before
// any of these are called.
#include "esp_err.h"
#include <string>

namespace nvs_config {

bool has_wifi_credentials();
esp_err_t set_wifi_credentials(const std::string &ssid, const std::string &password);
esp_err_t get_wifi_credentials(std::string &ssid, std::string &password);
esp_err_t clear_wifi_credentials();

bool has_anthropic_key();
esp_err_t set_anthropic_key(const std::string &key);
esp_err_t get_anthropic_key(std::string &key);

bool has_openai_key();
esp_err_t set_openai_key(const std::string &key);
esp_err_t get_openai_key(std::string &key);

// "sk-...ab12" style masking for on-screen display — never show a saved secret in full.
std::string mask_secret(const std::string &secret);

// Local LLM (LM Studio / Ollama) config — an alternative to the Anthropic API for the reply
// step, talking to a server on the local network via its OpenAI-compatible
// /v1/chat/completions endpoint instead. No API key needed (LAN-only, no auth by default).
bool get_use_local_llm();
esp_err_t set_use_local_llm(bool enabled);

bool has_local_llm_url();
esp_err_t set_local_llm_url(const std::string &url); // e.g. "http://192.168.1.50:1234" (LM Studio
                                                       // default port) or "http://host:11434" (Ollama)
esp_err_t get_local_llm_url(std::string &url);

// Model name to send in the request body. LM Studio ignores this (uses whatever's loaded) but
// Ollama's OpenAI-compat endpoint requires it to match a pulled model tag exactly (e.g.
// "llama3.2"). Defaults to "local-model" if never set.
esp_err_t set_local_llm_model(const std::string &model);
esp_err_t get_local_llm_model(std::string &model);

} // namespace nvs_config
