#pragma once
// Minimal screen-stack helper so every capability screen doesn't reinvent navigation. Each
// screen module exposes a create() that returns a standalone lv_obj_t screen (lv_obj_create(NULL));
// launcher tiles call nav::push() with it, and nav::add_back_button() gives every screen a
// consistent top-left back button wired to nav::pop().
#include "lvgl.h"

namespace nav {

// Call once from main.cpp after building the launcher screen.
void init(lv_obj_t *home_screen);

// Switches to `screen` and remembers the current one so pop() can return to it. `screen` is
// deleted automatically when the user navigates back past it.
void push(lv_obj_t *screen);

// Returns to the previous screen in the stack (no-op if already at home).
void pop();

// Adds a standard "< Back" button to the top-left of `parent`, wired to pop().
lv_obj_t *add_back_button(lv_obj_t *parent);

} // namespace nav
