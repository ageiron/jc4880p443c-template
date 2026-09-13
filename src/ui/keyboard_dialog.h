#pragma once
// Reusable on-device text-entry modal (LVGL keyboard widget) — used by the WiFi-connect and
// Settings (API key) screens so neither has to build its own keyboard handling.
#include "lvgl.h"
#include <functional>
#include <string>

namespace keyboard_dialog {

using SubmitCb = std::function<void(const std::string &value)>;
using SubmitCb2 = std::function<void(const std::string &field1, const std::string &field2)>;

// One text field (e.g. an API key). `password` masks the input as it's typed.
void show_single(lv_obj_t *parent, const char *title, const char *placeholder, bool password,
                  SubmitCb on_submit);

// Two text fields (SSID + password) for WiFi connect. Second field is always password-masked.
void show_two_field(lv_obj_t *parent, const char *title, const char *field1_placeholder,
                     const char *field2_placeholder, SubmitCb2 on_submit);

} // namespace keyboard_dialog
