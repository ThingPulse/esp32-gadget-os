#pragma once
#include <lvgl.h>
// Install wider letter rows and release-to-type controls on an existing keyboard.
// Enable a newline key in every text layout for multiline fields.
void configure_touch_keyboard(lv_obj_t *keyboard, bool multiline = false);
