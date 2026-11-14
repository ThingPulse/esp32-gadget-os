#pragma once
#include <lvgl.h>
// Install wider letter rows and release-to-type controls on an existing keyboard.
// Enable a newline key in every text layout for multiline fields.
void configure_touch_keyboard(lv_obj_t *keyboard, bool multiline = false);

// Consistent, steady caret for the active field in every built-in/package form.
void configure_text_input(lv_obj_t *textarea);
void attach_touch_keyboard(lv_obj_t *keyboard, lv_obj_t *textarea);
