#include "keyboard_dialog.h"

namespace keyboard_dialog {

static lv_obj_t *make_overlay(lv_obj_t *parent) {
    lv_obj_t *overlay = lv_obj_create(parent);
    lv_obj_remove_style_all(overlay);
    lv_obj_set_size(overlay, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(overlay, lv_color_hex(0x11111b), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(overlay, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_align(overlay, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_clear_flag(overlay, LV_OBJ_FLAG_SCROLLABLE);
    return overlay;
}

static lv_obj_t *make_title(lv_obj_t *overlay, const char *title) {
    lv_obj_t *label = lv_label_create(overlay);
    lv_label_set_text(label, title);
    lv_obj_set_style_text_color(label, lv_color_hex(0xCDD6F4), LV_PART_MAIN);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_18, LV_PART_MAIN);
    lv_obj_align(label, LV_ALIGN_TOP_MID, 0, 15);
    return label;
}

static void ta_focus_cb(lv_event_t *e) {
    lv_obj_t *kb = static_cast<lv_obj_t *>(lv_event_get_user_data(e));
    lv_obj_t *ta = static_cast<lv_obj_t *>(lv_event_get_target(e));
    lv_keyboard_set_textarea(kb, ta);
}

// Toggles password masking on/off for the target textarea — without this there is no way to
// verify what was actually typed on a small on-screen keyboard before submitting, which made
// WiFi password typos very easy to make unnoticed (found via user testing on hardware:
// repeated "careful" entry attempts still failing to connect).
static void show_password_toggle_cb(lv_event_t *e) {
    lv_obj_t *sw = static_cast<lv_obj_t *>(lv_event_get_target(e));
    auto *ta = static_cast<lv_obj_t *>(lv_event_get_user_data(e));
    lv_textarea_set_password_mode(ta, !lv_obj_has_state(sw, LV_STATE_CHECKED));
}

// ---- single field -----------------------------------------------------------

struct SingleCtx {
    lv_obj_t *overlay;
    lv_obj_t *ta;
    SubmitCb cb;
};

static void single_save_cb(lv_event_t *e) {
    auto *ctx = static_cast<SingleCtx *>(lv_event_get_user_data(e));
    std::string value = lv_textarea_get_text(ctx->ta);
    SubmitCb cb = ctx->cb;
    lv_obj_t *overlay = ctx->overlay;
    delete ctx;
    lv_obj_del(overlay);
    if (cb) cb(value);
}

static void single_cancel_cb(lv_event_t *e) {
    auto *ctx = static_cast<SingleCtx *>(lv_event_get_user_data(e));
    lv_obj_t *overlay = ctx->overlay;
    delete ctx;
    lv_obj_del(overlay);
}

void show_single(lv_obj_t *parent, const char *title, const char *placeholder, bool password,
                  SubmitCb on_submit) {
    lv_obj_t *overlay = make_overlay(parent);
    make_title(overlay, title);

    lv_obj_t *ta = lv_textarea_create(overlay);
    lv_textarea_set_one_line(ta, true);
    lv_textarea_set_password_mode(ta, password);
    lv_textarea_set_placeholder_text(ta, placeholder);
    lv_obj_set_width(ta, LV_PCT(85));
    lv_obj_align(ta, LV_ALIGN_TOP_MID, 0, 55);

    lv_obj_t *kb = lv_keyboard_create(overlay);
    lv_keyboard_set_textarea(kb, ta);
    lv_obj_align(kb, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_add_event_cb(ta, ta_focus_cb, LV_EVENT_FOCUSED, kb);

    auto *ctx = new SingleCtx{overlay, ta, std::move(on_submit)};

    lv_obj_t *save_btn = lv_btn_create(overlay);
    lv_obj_set_size(save_btn, 90, 45);
    lv_obj_align(save_btn, LV_ALIGN_TOP_RIGHT, -10, 100);
    lv_obj_add_event_cb(save_btn, single_save_cb, LV_EVENT_CLICKED, ctx);
    lv_obj_t *save_lbl = lv_label_create(save_btn);
    lv_label_set_text(save_lbl, "Save");
    lv_obj_center(save_lbl);

    lv_obj_t *cancel_btn = lv_btn_create(overlay);
    lv_obj_set_size(cancel_btn, 90, 45);
    lv_obj_align(cancel_btn, LV_ALIGN_TOP_LEFT, 10, 100);
    lv_obj_add_event_cb(cancel_btn, single_cancel_cb, LV_EVENT_CLICKED, ctx);
    lv_obj_t *cancel_lbl = lv_label_create(cancel_btn);
    lv_label_set_text(cancel_lbl, "Cancel");
    lv_obj_center(cancel_lbl);

    lv_obj_add_state(ta, LV_STATE_FOCUSED);
}

// ---- two field (WiFi SSID + password) ----------------------------------------

struct TwoFieldCtx {
    lv_obj_t *overlay;
    lv_obj_t *ta1;
    lv_obj_t *ta2;
    SubmitCb2 cb;
};

static void two_save_cb(lv_event_t *e) {
    auto *ctx = static_cast<TwoFieldCtx *>(lv_event_get_user_data(e));
    std::string v1 = lv_textarea_get_text(ctx->ta1);
    std::string v2 = lv_textarea_get_text(ctx->ta2);
    SubmitCb2 cb = ctx->cb;
    lv_obj_t *overlay = ctx->overlay;
    delete ctx;
    lv_obj_del(overlay);
    if (cb) cb(v1, v2);
}

static void two_cancel_cb(lv_event_t *e) {
    auto *ctx = static_cast<TwoFieldCtx *>(lv_event_get_user_data(e));
    lv_obj_t *overlay = ctx->overlay;
    delete ctx;
    lv_obj_del(overlay);
}

void show_two_field(lv_obj_t *parent, const char *title, const char *field1_placeholder,
                     const char *field2_placeholder, SubmitCb2 on_submit) {
    lv_obj_t *overlay = make_overlay(parent);
    make_title(overlay, title);

    lv_obj_t *ta1 = lv_textarea_create(overlay);
    lv_textarea_set_one_line(ta1, true);
    lv_textarea_set_placeholder_text(ta1, field1_placeholder);
    lv_obj_set_width(ta1, LV_PCT(85));
    lv_obj_align(ta1, LV_ALIGN_TOP_MID, 0, 55);

    lv_obj_t *ta2 = lv_textarea_create(overlay);
    lv_textarea_set_one_line(ta2, true);
    lv_textarea_set_password_mode(ta2, true);
    lv_textarea_set_placeholder_text(ta2, field2_placeholder);
    lv_obj_set_width(ta2, LV_PCT(85));
    lv_obj_align_to(ta2, ta1, LV_ALIGN_OUT_BOTTOM_MID, 0, 15);

    // "Show password" toggle — see show_password_toggle_cb's comment for why this exists.
    lv_obj_t *show_row = lv_obj_create(overlay);
    lv_obj_remove_style_all(show_row);
    lv_obj_set_size(show_row, LV_PCT(85), 30);
    lv_obj_clear_flag(show_row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_align_to(show_row, ta2, LV_ALIGN_OUT_BOTTOM_MID, 0, 8);

    lv_obj_t *show_switch = lv_switch_create(show_row);
    lv_obj_align(show_switch, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_add_event_cb(show_switch, show_password_toggle_cb, LV_EVENT_VALUE_CHANGED, ta2);

    lv_obj_t *show_lbl = lv_label_create(show_row);
    lv_label_set_text(show_lbl, "Show password");
    lv_obj_set_style_text_color(show_lbl, lv_color_hex(0xA6ADC8), LV_PART_MAIN);
    lv_obj_align_to(show_lbl, show_switch, LV_ALIGN_OUT_RIGHT_MID, 8, 0);

    lv_obj_t *kb = lv_keyboard_create(overlay);
    lv_keyboard_set_textarea(kb, ta1);
    lv_obj_align(kb, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_add_event_cb(ta1, ta_focus_cb, LV_EVENT_FOCUSED, kb);
    lv_obj_add_event_cb(ta2, ta_focus_cb, LV_EVENT_FOCUSED, kb);

    auto *ctx = new TwoFieldCtx{overlay, ta1, ta2, std::move(on_submit)};

    lv_obj_t *save_btn = lv_btn_create(overlay);
    lv_obj_set_size(save_btn, 90, 45);
    lv_obj_align_to(save_btn, show_row, LV_ALIGN_OUT_BOTTOM_RIGHT, 0, 12);
    lv_obj_add_event_cb(save_btn, two_save_cb, LV_EVENT_CLICKED, ctx);
    lv_obj_t *save_lbl = lv_label_create(save_btn);
    lv_label_set_text(save_lbl, "Save");
    lv_obj_center(save_lbl);

    lv_obj_t *cancel_btn = lv_btn_create(overlay);
    lv_obj_set_size(cancel_btn, 90, 45);
    lv_obj_align_to(cancel_btn, show_row, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 12);
    lv_obj_add_event_cb(cancel_btn, two_cancel_cb, LV_EVENT_CLICKED, ctx);
    lv_obj_t *cancel_lbl = lv_label_create(cancel_btn);
    lv_label_set_text(cancel_lbl, "Cancel");
    lv_obj_center(cancel_lbl);

    lv_obj_add_state(ta1, LV_STATE_FOCUSED);
}

} // namespace keyboard_dialog
