#pragma once
// Portrait/landscape toggle. main.cpp registers the lv_display_t handle once at boot (after
// lvgl_port_add_disp_dsi(), with disp_cfg.flags.sw_rotate = true so this works without needing
// LCD-panel-level swap_xy support — see docs/BRINGUP.md "Portrait/landscape toggle").
#include "lvgl.h"

namespace display_service {

// Called once from app_main() right after the display is registered with LVGL.
void init(lv_display_t *disp);

bool is_landscape();

// Safe to call directly from an LVGL event callback (e.g. a button's click handler) — takes
// esp_lvgl_port's lock itself, and that lock is a recursive mutex.
void set_landscape(bool landscape);
void toggle();

} // namespace display_service
