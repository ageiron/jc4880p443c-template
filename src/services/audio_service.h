#pragma once
// ES8311 audio codec (speaker DAC out + built-in mic ADC in, one chip — see
// docs/BRINGUP.md "Audio codec bring-up"). Must run after display/touch bring-up in
// app_main() because it reuses the shared I2C bus (see i2c_bus.h).
#include "esp_err.h"
#include <cstdint>
#include <cstddef>

namespace audio_service {

// Idempotent. Called lazily by the other functions if not already done.
esp_err_t init();

esp_err_t set_volume(int volume_pct); // 0-100

// Blocking: generates and plays a sine tone through the speaker.
esp_err_t play_tone(uint32_t freq_hz, uint32_t duration_ms);

// Blocking: plays raw 16-bit PCM at the given sample rate/channel count through the speaker.
esp_err_t play_pcm(const uint8_t *pcm_data, size_t len, uint32_t sample_rate, int channels = 1);

esp_err_t start_capture();
esp_err_t read_capture(uint8_t *buf, size_t len, size_t *bytes_read);
esp_err_t stop_capture();
bool is_capturing();

// Convenience for a VU meter: reads a short chunk and returns its RMS level, 0-100.
int get_input_level();

} // namespace audio_service
