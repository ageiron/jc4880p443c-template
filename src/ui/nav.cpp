#include "nav.h"
#include <vector>

namespace nav {

static std::vector<lv_obj_t *> s_stack;

void init(lv_obj_t *home_screen) {
    s_stack.clear();
    s_stack.push_back(home_screen);
    lv_scr_load(home_screen);
}

void push(lv_obj_t *screen) {
    s_stack.push_back(screen);
    lv_scr_load_anim(screen, LV_SCR_LOAD_ANIM_MOVE_LEFT, 200, 0, false);
}

void pop() {
    if (s_stack.size() <= 1) return;
    s_stack.pop_back();
    lv_obj_t *prev = s_stack.back();
    // auto_del=true deletes the screen we're leaving once the animation completes.
    lv_scr_load_anim(prev, LV_SCR_LOAD_ANIM_MOVE_RIGHT, 200, 0, true);
}

static void back_btn_cb(lv_event_t *e) {
    (void)e;
    pop();
}

lv_obj_t *add_back_button(lv_obj_t *parent) {
    lv_obj_t *btn = lv_btn_create(parent);
    lv_obj_set_size(btn, 90, 50);
    lv_obj_align(btn, LV_ALIGN_TOP_LEFT, 10, 10);
    lv_obj_add_event_cb(btn, back_btn_cb, LV_EVENT_CLICKED, nullptr);

    lv_obj_t *lbl = lv_label_create(btn);
    lv_label_set_text(lbl, LV_SYMBOL_LEFT " Back");
    lv_obj_center(lbl);
    return btn;
}

} // namespace nav
