#include "display_service.h"
#include "esp_lvgl_port.h"

namespace display_service {

static lv_display_t *s_disp = nullptr;

void init(lv_display_t *disp) { s_disp = disp; }

bool is_landscape() {
    if (!s_disp) return false;
    lv_display_rotation_t rot = lv_display_get_rotation(s_disp);
    return rot == LV_DISPLAY_ROTATION_90 || rot == LV_DISPLAY_ROTATION_270;
}

void set_landscape(bool landscape) {
    if (!s_disp) return;
    if (!lvgl_port_lock(0)) return;
    lv_display_set_rotation(s_disp, landscape ? LV_DISPLAY_ROTATION_90 : LV_DISPLAY_ROTATION_0);
    lvgl_port_unlock();
}

void toggle() { set_landscape(!is_landscape()); }

} // namespace display_service
