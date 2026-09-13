#include "screen_assistant.h"
#include "nav.h"
#include "../services/ai_service.h"
#include "esp_lvgl_port.h"
#include <string>

namespace screen_assistant {

static lv_obj_t *s_state_label = nullptr;
static lv_obj_t *s_transcript_list = nullptr;
static lv_obj_t *s_warning_label = nullptr;
static lv_obj_t *s_talk_btn = nullptr;
static lv_obj_t *s_talk_btn_lbl = nullptr;

static const char *state_text(ai_service::State s) {
    switch (s) {
        case ai_service::State::Idle: return "Hold the button and speak";
        case ai_service::State::Recording: return "Listening...";
        case ai_service::State::Transcribing: return "Transcribing...";
        case ai_service::State::Thinking: return "Thinking...";
        case ai_service::State::Speaking: return "Speaking...";
        case ai_service::State::Error: return "Error";
        default: return "";
    }
}

static void refresh_warning() {
    auto pre = ai_service::check_prerequisites();
    if (pre.wifi_ok && pre.reply_backend_ok && pre.openai_key_ok) {
        lv_obj_add_flag(s_warning_label, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    std::string msg = "Missing before this works: ";
    bool first = true;
    if (!pre.wifi_ok) { msg += "WiFi connection"; first = false; }
    if (!pre.reply_backend_ok) {
        msg += (first ? "" : ", ");
        msg += pre.using_local_llm ? "local LLM URL" : "Anthropic key";
        first = false;
    }
    if (!pre.openai_key_ok) { msg += (first ? "" : ", "); msg += "OpenAI key"; }
    lv_label_set_text(s_warning_label, msg.c_str());
    lv_obj_clear_flag(s_warning_label, LV_OBJ_FLAG_HIDDEN);
}

static void on_state(ai_service::State state, const std::string &detail) {
    if (!lvgl_port_lock(0)) return;
    lv_label_set_text(s_state_label, state_text(state));
    if (state == ai_service::State::Error) {
        lv_list_add_text(s_transcript_list, detail.c_str());
    }
    bool busy = (state != ai_service::State::Idle && state != ai_service::State::Error);
    if (!busy) {
        lv_label_set_text(s_talk_btn_lbl, LV_SYMBOL_AUDIO " Hold to Talk");
    }
    lvgl_port_unlock();
}

static void on_transcript(const std::string &user_text, const std::string &assistant_text) {
    if (!lvgl_port_lock(0)) return;
    std::string you_line = "You: " + user_text;
    std::string ai_line = "Assistant: " + assistant_text;
    lv_list_add_text(s_transcript_list, you_line.c_str());
    lv_list_add_text(s_transcript_list, ai_line.c_str());
    lvgl_port_unlock();
}

static void talk_pressed_cb(lv_event_t *e) {
    (void)e;
    refresh_warning();
    auto pre = ai_service::check_prerequisites();
    if (!(pre.wifi_ok && pre.reply_backend_ok && pre.openai_key_ok)) return;
    lv_label_set_text(s_talk_btn_lbl, LV_SYMBOL_AUDIO " Listening...");
    ai_service::start_turn(15, on_state, on_transcript);
}

static void talk_released_cb(lv_event_t *e) {
    (void)e;
    ai_service::stop_recording();
}

lv_obj_t *create() {
    lv_obj_t *scr = lv_obj_create(nullptr);
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x181825), LV_PART_MAIN);
    nav::add_back_button(scr);

    lv_obj_t *title = lv_label_create(scr);
    lv_label_set_text(title, "AI Voice Assistant");
    lv_obj_set_style_text_color(title, lv_color_hex(0xCDD6F4), LV_PART_MAIN);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_32, LV_PART_MAIN);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 30);

    s_warning_label = lv_label_create(scr);
    lv_label_set_long_mode(s_warning_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(s_warning_label, LV_PCT(85));
    lv_obj_set_style_text_align(s_warning_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_color(s_warning_label, lv_color_hex(0xF38BA8), LV_PART_MAIN);
    lv_obj_align_to(s_warning_label, title, LV_ALIGN_OUT_BOTTOM_MID, 0, 8);

    s_state_label = lv_label_create(scr);
    lv_label_set_text(s_state_label, state_text(ai_service::State::Idle));
    lv_obj_set_style_text_color(s_state_label, lv_color_hex(0xA6E3A1), LV_PART_MAIN);
    lv_obj_align(s_state_label, LV_ALIGN_TOP_MID, 0, 100);

    s_transcript_list = lv_list_create(scr);
    lv_obj_set_size(s_transcript_list, LV_PCT(90), 420);
    lv_obj_align(s_transcript_list, LV_ALIGN_TOP_MID, 0, 130);
    lv_list_add_text(s_transcript_list, "Conversation will appear here.");

    s_talk_btn = lv_btn_create(scr);
    lv_obj_set_size(s_talk_btn, 260, 70);
    lv_obj_align(s_talk_btn, LV_ALIGN_BOTTOM_MID, 0, -20);
    lv_obj_add_event_cb(s_talk_btn, talk_pressed_cb, LV_EVENT_PRESSED, nullptr);
    lv_obj_add_event_cb(s_talk_btn, talk_released_cb, LV_EVENT_RELEASED, nullptr);
    s_talk_btn_lbl = lv_label_create(s_talk_btn);
    lv_label_set_text(s_talk_btn_lbl, LV_SYMBOL_AUDIO " Hold to Talk");
    lv_obj_center(s_talk_btn_lbl);

    refresh_warning();

    return scr;
}

} // namespace screen_assistant
