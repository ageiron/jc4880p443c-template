#include "screen_settings.h"
#include "nav.h"
#include "keyboard_dialog.h"
#include "../services/nvs_config.h"
#include <string>

namespace screen_settings {

static lv_obj_t *s_anthropic_label = nullptr;
static lv_obj_t *s_openai_label = nullptr;
static lv_obj_t *s_local_llm_switch = nullptr;
static lv_obj_t *s_local_llm_url_label = nullptr;
static lv_obj_t *s_local_llm_model_label = nullptr;
static lv_obj_t *s_anthropic_row_btn = nullptr;

static void refresh_labels() {
    std::string key;
    if (nvs_config::get_anthropic_key(key) == ESP_OK && !key.empty()) {
        lv_label_set_text_fmt(s_anthropic_label, "Anthropic key: %s", nvs_config::mask_secret(key).c_str());
    } else {
        lv_label_set_text(s_anthropic_label, "Anthropic key: not set");
    }
    if (nvs_config::get_openai_key(key) == ESP_OK && !key.empty()) {
        lv_label_set_text_fmt(s_openai_label, "OpenAI key: %s", nvs_config::mask_secret(key).c_str());
    } else {
        lv_label_set_text(s_openai_label, "OpenAI key: not set");
    }

    bool use_local = nvs_config::get_use_local_llm();
    if (use_local) lv_obj_add_state(s_local_llm_switch, LV_STATE_CHECKED);
    else lv_obj_clear_state(s_local_llm_switch, LV_STATE_CHECKED);

    std::string url;
    if (nvs_config::get_local_llm_url(url) == ESP_OK && !url.empty()) {
        lv_label_set_text_fmt(s_local_llm_url_label, "Local LLM URL: %s", url.c_str());
    } else {
        lv_label_set_text(s_local_llm_url_label, "Local LLM URL: not set");
    }
    std::string model;
    nvs_config::get_local_llm_model(model);
    lv_label_set_text_fmt(s_local_llm_model_label, "Local model: %s", model.c_str());

    // Dims to make clear which reply backend is actually in effect.
    lv_opa_t local_opa = use_local ? LV_OPA_COVER : LV_OPA_50;
    lv_opa_t anthropic_opa = use_local ? LV_OPA_50 : LV_OPA_COVER;
    lv_obj_set_style_text_opa(s_local_llm_url_label, local_opa, LV_PART_MAIN);
    lv_obj_set_style_text_opa(s_local_llm_model_label, local_opa, LV_PART_MAIN);
    lv_obj_set_style_text_opa(s_anthropic_label, anthropic_opa, LV_PART_MAIN);
    lv_obj_set_style_opa(s_anthropic_row_btn, anthropic_opa, LV_PART_MAIN);
}

static void set_anthropic_btn_cb(lv_event_t *e) {
    (void)e;
    keyboard_dialog::show_single(lv_scr_act(), "Anthropic API key", "sk-ant-...", true,
        [](const std::string &value) {
            if (!value.empty()) nvs_config::set_anthropic_key(value);
            refresh_labels();
        });
}

static void set_openai_btn_cb(lv_event_t *e) {
    (void)e;
    keyboard_dialog::show_single(lv_scr_act(), "OpenAI API key", "sk-...", true,
        [](const std::string &value) {
            if (!value.empty()) nvs_config::set_openai_key(value);
            refresh_labels();
        });
}

static void local_llm_toggle_cb(lv_event_t *e) {
    lv_obj_t *sw = static_cast<lv_obj_t *>(lv_event_get_target(e));
    nvs_config::set_use_local_llm(lv_obj_has_state(sw, LV_STATE_CHECKED));
    refresh_labels();
}

static void set_local_llm_url_btn_cb(lv_event_t *e) {
    (void)e;
    keyboard_dialog::show_single(lv_scr_act(), "Local LLM URL", "http://192.168.1.50:1234", false,
        [](const std::string &value) {
            if (!value.empty()) nvs_config::set_local_llm_url(value);
            refresh_labels();
        });
}

static void set_local_llm_model_btn_cb(lv_event_t *e) {
    (void)e;
    keyboard_dialog::show_single(lv_scr_act(), "Local model name (Ollama needs this exact)", "local-model", false,
        [](const std::string &value) {
            nvs_config::set_local_llm_model(value);
            refresh_labels();
        });
}

static lv_obj_t *make_row(lv_obj_t *scr, int y_offset, const char *set_btn_label,
                           lv_event_cb_t cb, lv_obj_t **out_label) {
    lv_obj_t *label = lv_label_create(scr);
    lv_obj_set_style_text_color(label, lv_color_hex(0xCDD6F4), LV_PART_MAIN);
    lv_obj_align(label, LV_ALIGN_TOP_MID, -60, y_offset);
    *out_label = label;

    lv_obj_t *btn = lv_btn_create(scr);
    lv_obj_set_size(btn, 100, 45);
    lv_obj_align(btn, LV_ALIGN_TOP_MID, 160, y_offset - 10);
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, nullptr);
    lv_obj_t *btn_lbl = lv_label_create(btn);
    lv_label_set_text(btn_lbl, set_btn_label);
    lv_obj_center(btn_lbl);
    return btn;
}

lv_obj_t *create() {
    lv_obj_t *scr = lv_obj_create(nullptr);
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x181825), LV_PART_MAIN);
    nav::add_back_button(scr);

    lv_obj_t *title = lv_label_create(scr);
    lv_label_set_text(title, "Settings");
    lv_obj_set_style_text_color(title, lv_color_hex(0xCDD6F4), LV_PART_MAIN);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_32, LV_PART_MAIN);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 30);

    lv_obj_t *note = lv_label_create(scr);
    lv_label_set_long_mode(note, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(note, LV_PCT(85));
    lv_obj_set_style_text_align(note, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_label_set_text(note, "Needed for the AI voice assistant screen. Stored in NVS on-device, never "
                             "hardcoded. Keys are long -- easiest set over USB serial (SET_ANTHROPIC_KEY:/"
                             "SET_OPENAI_KEY: in the serial monitor). The buttons below use the on-screen "
                             "keyboard instead, if you'd rather type it here.");
    lv_obj_set_style_text_color(note, lv_color_hex(0x6c7086), LV_PART_MAIN);
    lv_obj_align_to(note, title, LV_ALIGN_OUT_BOTTOM_MID, 0, 8);

    make_row(scr, 190, "Set", set_openai_btn_cb, &s_openai_label);
    s_anthropic_row_btn = make_row(scr, 260, "Set", set_anthropic_btn_cb, &s_anthropic_label);

    lv_obj_t *sep = lv_label_create(scr);
    lv_label_set_text(sep, "Reply backend (pick one):");
    lv_obj_set_style_text_color(sep, lv_color_hex(0x89B4FA), LV_PART_MAIN);
    lv_obj_align(sep, LV_ALIGN_TOP_MID, 0, 320);

    lv_obj_t *toggle_lbl = lv_label_create(scr);
    lv_label_set_text(toggle_lbl, "Use local LLM (LM Studio/Ollama)");
    lv_obj_set_style_text_color(toggle_lbl, lv_color_hex(0xCDD6F4), LV_PART_MAIN);
    lv_obj_align(toggle_lbl, LV_ALIGN_TOP_MID, -70, 360);

    s_local_llm_switch = lv_switch_create(scr);
    lv_obj_align(s_local_llm_switch, LV_ALIGN_TOP_MID, 150, 355);
    lv_obj_add_event_cb(s_local_llm_switch, local_llm_toggle_cb, LV_EVENT_VALUE_CHANGED, nullptr);

    make_row(scr, 410, "Set", set_local_llm_url_btn_cb, &s_local_llm_url_label);
    make_row(scr, 470, "Set", set_local_llm_model_btn_cb, &s_local_llm_model_label);

    lv_obj_t *local_note = lv_label_create(scr);
    lv_label_set_long_mode(local_note, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(local_note, LV_PCT(85));
    lv_obj_set_style_text_align(local_note, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_label_set_text(local_note, "When on, the AI Assistant sends the reply step to this LAN server's "
                                   "OpenAI-compatible /v1/chat/completions endpoint instead of Anthropic — "
                                   "no API key needed. Whisper (speech-to-text) and TTS still use OpenAI "
                                   "either way, so the OpenAI key above is always required.");
    lv_obj_set_style_text_color(local_note, lv_color_hex(0x6c7086), LV_PART_MAIN);
    lv_obj_align(local_note, LV_ALIGN_TOP_MID, 0, 520);

    refresh_labels();

    return scr;
}

} // namespace screen_settings
