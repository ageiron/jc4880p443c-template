#pragma once
#include "lvgl.h"

namespace launcher {
// Builds and returns the home screen. Does not load it — call nav::init() with the result.
lv_obj_t *create();
}
