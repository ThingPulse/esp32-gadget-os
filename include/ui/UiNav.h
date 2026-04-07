#pragma once

#include <lvgl.h>
#include "ui/UiTrace.h"

inline void ui_load_screen(lv_obj_t *target, bool delete_previous)
{
  if (!target || !lv_obj_is_valid(target)) return;

  lv_obj_t *previous = lv_screen_active();
  UI_TRACE("Nav", "ui_load_screen delete_previous=%d prev=%p target=%p", delete_previous ? 1 : 0, (void*)previous, (void*)target);
  if (previous == target) {
    lv_screen_load(target);
    return;
  }

  lv_screen_load(target);

  if (delete_previous && previous && lv_obj_is_valid(previous)) {
    lv_obj_delete_async(previous);
  }
}
