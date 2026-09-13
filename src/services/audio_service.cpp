#include "audio_service.h"
#include "i2c_bus.h"
#include "driver/i2s_std.h"
#include "driver/gpio.h"
#include "esp_codec_dev.h"
#include "esp_codec_dev_defaults.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <vector>

namespace audio_service {

static const char *TAG = "audio_service";
static std::atomic<bool> s_stop_playback_requested{false};

// Confirmed against vendor mp3_player.ino, the CODEC&TFCARD schematic, AND independently
// against xiaozhi-esp32's shipping "guition-jc4880p443" board port (exact board match) — see
// docs/BRINGUP.md "Audio codec bring-up".
constexpr gpio_num_t I2S_MCLK_PIN = GPIO_NUM_13;
constexpr gpio_num_t I2S_BCLK_PIN = GPIO_NUM_12;
constexpr gpio_num_t I2S_WS_PIN = GPIO_NUM_10;
constexpr gpio_num_t I2S_DOUT_PIN = GPIO_NUM_9;  // ESP -> codec (speaker)
constexpr gpio_num_t I2S_DIN_PIN = GPIO_NUM_48;  // codec -> ESP (mic)
constexpr gpio_num_t PA_ENABLE_PIN = GPIO_NUM_11;
// NOT 0x18 (the ES8311 datasheet's 7-bit address, which is what's silkscreened/visible on the
// schematic) — esp_codec_dev's audio_codec_i2c_cfg_t.addr wants the 8-bit address, 0x30
// (ES8311_CODEC_DEFAULT_ADDR in managed_components/espressif__esp_codec_dev/device/include/es8311_codec.h).
// Using 0x18 here produced "Fail to write to dev 18" / zero ACKs on every I2C transaction —
// found by on-hardware testing, see docs/BRINGUP.md "Audio codec bring-up".
constexpr uint8_t ES8311_I2C_ADDR = 0x30;
constexpr uint32_t DEFAULT_SAMPLE_RATE = 16000; // good enough for voice; not music-fidelity

static i2s_chan_handle_t s_tx_chan = nullptr;
static i2s_chan_handle_t s_rx_chan = nullptr;
static const audio_codec_data_if_t *s_i2s_data_if = nullptr;
static const audio_codec_if_t *s_codec_if = nullptr;
// One shared duplex device for both directions, matching xiaozhi-esp32's proven
// guition-jc4880p443 board port — two separate esp_codec_dev instances (one IN, one OUT) both
// wrapping the same I2S data_if did not produce any audio on this exact board/chip.
static esp_codec_dev_handle_t s_dev = nullptr;
static uint32_t s_open_sample_rate = 0;
static bool s_inited = false;
static bool s_capturing = false;

static esp_err_t init_i2s() {
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    chan_cfg.auto_clear = true;
    esp_err_t err = i2s_new_channel(&chan_cfg, &s_tx_chan, &s_rx_chan);
    if (err != ESP_OK) return err;

    // NOTE: I2S_SLOT_MODE_STEREO/I2S_STD_SLOT_BOTH here, even though we only ever carry mono
    // content — the ES8311's I2S timing expects both slots active; esp_codec_dev's mono
    // sample_info (set at open() time below) handles the down/up-mixing on top of this. Using
    // I2S_SLOT_MODE_MONO here (the more "obvious" choice) produced no audio at all on hardware.
    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(DEFAULT_SAMPLE_RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = I2S_MCLK_PIN,
            .bclk = I2S_BCLK_PIN,
            .ws = I2S_WS_PIN,
            .dout = I2S_DOUT_PIN,
            .din = I2S_DIN_PIN,
            .invert_flags = {.mclk_inv = false, .bclk_inv = false, .ws_inv = false},
        },
    };

    if (s_tx_chan) {
        err = i2s_channel_init_std_mode(s_tx_chan, &std_cfg);
        if (err != ESP_OK) return err;
        err = i2s_channel_enable(s_tx_chan);
        if (err != ESP_OK) return err;
    }
    if (s_rx_chan) {
        err = i2s_channel_init_std_mode(s_rx_chan, &std_cfg);
        if (err != ESP_OK) return err;
        err = i2s_channel_enable(s_rx_chan);
        if (err != ESP_OK) return err;
    }

    audio_codec_i2s_cfg_t i2s_cfg = {};
    i2s_cfg.port = I2S_NUM_0;
    i2s_cfg.tx_handle = s_tx_chan;
    i2s_cfg.rx_handle = s_rx_chan;
    s_i2s_data_if = audio_codec_new_i2s_data(&i2s_cfg);
    return s_i2s_data_if ? ESP_OK : ESP_FAIL;
}

static uint32_t s_open_channels = 0;

static esp_err_t open_dev(uint32_t sample_rate, int channels = 1) {
    if (s_dev && s_open_sample_rate == sample_rate && s_open_channels == (uint32_t)channels) return ESP_OK;
    if (s_dev) esp_codec_dev_close(s_dev);

    esp_codec_dev_sample_info_t fs = {};
    fs.bits_per_sample = 16;
    fs.channel = channels;
    fs.sample_rate = sample_rate;
    esp_err_t err = esp_codec_dev_open(s_dev, &fs);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_codec_dev_open failed: %s", esp_err_to_name(err));
        return err;
    }
    s_open_sample_rate = sample_rate;
    s_open_channels = channels;
    esp_codec_dev_set_in_gain(s_dev, 30);
    return ESP_OK;
}

esp_err_t init() {
    if (s_inited) return ESP_OK;

    gpio_config_t pa_cfg = {};
    pa_cfg.pin_bit_mask = 1ULL << PA_ENABLE_PIN;
    pa_cfg.mode = GPIO_MODE_OUTPUT;
    gpio_config(&pa_cfg);
    gpio_set_level(PA_ENABLE_PIN, 1);

    i2c_master_bus_handle_t i2c = i2c_bus_registry::get_shared_bus();
    if (!i2c) {
        ESP_LOGE(TAG, "Shared I2C bus not set — audio_service::init() must run after display/touch bring-up");
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t err = init_i2s();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "I2S init failed: %s", esp_err_to_name(err));
        return err;
    }

    const audio_codec_gpio_if_t *gpio_if = audio_codec_new_gpio();

    audio_codec_i2c_cfg_t i2c_cfg = {};
    i2c_cfg.port = I2C_NUM_0;
    i2c_cfg.addr = ES8311_I2C_ADDR;
    i2c_cfg.bus_handle = i2c;
    const audio_codec_ctrl_if_t *i2c_ctrl_if = audio_codec_new_i2c_ctrl(&i2c_cfg);
    if (!i2c_ctrl_if) return ESP_FAIL;

    esp_codec_dev_hw_gain_t gain = {};
    gain.pa_voltage = 5.0;
    gain.codec_dac_voltage = 3.3;

    es8311_codec_cfg_t es8311_cfg = {};
    es8311_cfg.ctrl_if = i2c_ctrl_if;
    es8311_cfg.gpio_if = gpio_if;
    es8311_cfg.codec_mode = ESP_CODEC_DEV_WORK_MODE_BOTH;
    es8311_cfg.pa_pin = PA_ENABLE_PIN;
    es8311_cfg.pa_reverted = false;
    es8311_cfg.master_mode = false;
    es8311_cfg.use_mclk = true;
    es8311_cfg.digital_mic = false;
    es8311_cfg.hw_gain = gain;

    s_codec_if = es8311_codec_new(&es8311_cfg);
    if (!s_codec_if) return ESP_FAIL;

    esp_codec_dev_cfg_t dev_cfg = {};
    dev_cfg.dev_type = ESP_CODEC_DEV_TYPE_IN_OUT;
    dev_cfg.codec_if = s_codec_if;
    dev_cfg.data_if = s_i2s_data_if;
    s_dev = esp_codec_dev_new(&dev_cfg);
    if (!s_dev) return ESP_FAIL;

    err = open_dev(DEFAULT_SAMPLE_RATE);
    if (err != ESP_OK) return err;
    set_volume(70);

    s_inited = true;
    ESP_LOGI(TAG, "Audio codec initialised");
    return ESP_OK;
}

esp_err_t set_volume(int volume_pct) {
    if (!s_dev) return ESP_ERR_INVALID_STATE;
    volume_pct = std::clamp(volume_pct, 0, 100);
    return esp_codec_dev_set_out_vol(s_dev, volume_pct);
}

esp_err_t play_pcm(const uint8_t *pcm_data, size_t len, uint32_t sample_rate, int channels) {
    if (!s_inited) {
        esp_err_t e = init();
        if (e != ESP_OK) return e;
    }
    esp_err_t err = open_dev(sample_rate, channels);
    if (err != ESP_OK) return err;

    // Written in chunks, not one call, so stop_playback() actually has somewhere to interrupt —
    // esp_codec_dev_write() itself is a single blocking call with no way to cut it short once
    // started. ~85ms/chunk at 24kHz mono keeps "stop" feeling responsive without much per-call
    // overhead.
    s_stop_playback_requested = false;
    constexpr size_t CHUNK_BYTES = 4096;
    size_t offset = 0;
    while (offset < len) {
        if (s_stop_playback_requested) break;
        size_t chunk = std::min(CHUNK_BYTES, len - offset);
        esp_err_t chunk_err = esp_codec_dev_write(s_dev, const_cast<uint8_t *>(pcm_data + offset), chunk);
        if (chunk_err != ESP_OK) return chunk_err;
        offset += chunk;
    }
    return ESP_OK;
}

void stop_playback() { s_stop_playback_requested = true; }

esp_err_t play_tone(uint32_t freq_hz, uint32_t duration_ms) {
    if (!s_inited) {
        esp_err_t e = init();
        if (e != ESP_OK) return e;
    }
    size_t num_samples = (DEFAULT_SAMPLE_RATE * duration_ms) / 1000;
    std::vector<int16_t> buf(num_samples);
    for (size_t i = 0; i < num_samples; i++) {
        buf[i] = static_cast<int16_t>(6000.0 * sinf(2.0f * static_cast<float>(M_PI) * freq_hz * i / DEFAULT_SAMPLE_RATE));
    }
    return play_pcm(reinterpret_cast<const uint8_t *>(buf.data()), buf.size() * sizeof(int16_t), DEFAULT_SAMPLE_RATE);
}

esp_err_t start_capture() {
    if (!s_inited) {
        esp_err_t e = init();
        if (e != ESP_OK) return e;
    }
    esp_err_t err = open_dev(DEFAULT_SAMPLE_RATE);
    if (err != ESP_OK) return err;
    s_capturing = true;
    return ESP_OK;
}

esp_err_t read_capture(uint8_t *buf, size_t len, size_t *bytes_read) {
    if (!s_capturing) return ESP_ERR_INVALID_STATE;
    esp_err_t err = esp_codec_dev_read(s_dev, buf, len);
    if (bytes_read) *bytes_read = (err == ESP_OK) ? len : 0;
    return err;
}

esp_err_t stop_capture() {
    // The device stays open (it's shared with playback) — just stop treating reads as valid.
    s_capturing = false;
    return ESP_OK;
}

bool is_capturing() { return s_capturing; }

int get_input_level() {
    if (!s_capturing) return 0;
    static int16_t buf[256];
    size_t bytes_read = 0;
    if (read_capture(reinterpret_cast<uint8_t *>(buf), sizeof(buf), &bytes_read) != ESP_OK) return 0;
    size_t samples = bytes_read / sizeof(int16_t);
    if (samples == 0) return 0;
    double sum_sq = 0;
    for (size_t i = 0; i < samples; i++) sum_sq += static_cast<double>(buf[i]) * buf[i];
    double rms = sqrt(sum_sq / samples);
    int level = static_cast<int>(rms / 327.0); // ~0-32767 RMS scaled down to ~0-100
    return std::clamp(level, 0, 100);
}

} // namespace audio_service
