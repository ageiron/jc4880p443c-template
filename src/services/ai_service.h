#pragma once
// Orchestrates the tap-to-talk assistant pipeline:
//   mic (audio_service) --WAV--> OpenAI Whisper (STT)
//                                      |  transcribed text
//                                      v
//                    Anthropic Messages API, OR a local LM Studio/Ollama
//                    server on the LAN (nvs_config::get_use_local_llm())
//                                      |  reply text
//                                      v
//                             OpenAI TTS (raw PCM)
//                                      |
//                                      v
//                             speaker (audio_service)
// Requires WiFi connected (wifi_service), an OpenAI key set (for Whisper + TTS), and a reply
// backend configured — either the Anthropic key, or the local-LLM toggle + URL (nvs_config).
// check_prerequisites() before starting a turn so the UI can explain what's missing instead of
// failing silently.
#include <cstdint>
#include <functional>
#include <string>

namespace ai_service {

enum class State { Idle, Recording, Transcribing, Thinking, Speaking, Error };

struct Prerequisites {
    bool wifi_ok;
    bool reply_backend_ok; // Anthropic key set, or local-LLM toggle on + URL set
    bool openai_key_ok;    // always needed: Whisper STT + TTS
    bool using_local_llm;  // for the UI to know which of the above it's checking
};

Prerequisites check_prerequisites();

// Called on state transitions from a background task — hop through lvgl_port_lock()/unlock()
// before touching LVGL objects inside this callback. `detail` carries extra context (the
// transcribed text while Thinking, an error message on Error, empty otherwise).
using StateCb = std::function<void(State, const std::string &detail)>;

// Called once both the user's transcribed text and the assistant's reply text are available,
// so the UI can append both to a transcript view in one go.
using TranscriptCb = std::function<void(const std::string &user_text, const std::string &assistant_text)>;

// Starts recording immediately (records up to max_seconds unless stop_recording() is called
// first) then runs the rest of the pipeline. No-op if a turn is already in progress.
void start_turn(uint32_t max_seconds, StateCb on_state, TranscriptCb on_transcript);

// Ends recording early (press-and-hold-to-talk release).
void stop_recording();

// Cuts a long reply short mid-playback (State::Speaking). No-op otherwise.
void stop_speaking();

} // namespace ai_service
