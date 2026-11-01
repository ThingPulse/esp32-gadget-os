#pragma once
#include <lvgl.h>

// Keep the loading animation tied to the actual request state, including when
// entering the screen during a request or recovering from a missed transition.
inline void weather_loading_move(void *object, int32_t x)
{
    lv_obj_set_x(static_cast<lv_obj_t *>(object), x);
}

inline void weather_loading_set(lv_obj_t *line, bool fetching)
{
    if (!line || !lv_obj_is_valid(line)) return;
    if (!fetching) {
        lv_anim_delete(line, weather_loading_move);
        if (!lv_obj_has_flag(line, LV_OBJ_FLAG_HIDDEN)) {
            lv_obj_add_flag(line, LV_OBJ_FLAG_HIDDEN);
            // Clear the complete animation track, including its last position.
            lv_obj_invalidate(lv_obj_get_parent(line));
        }
        return;
    }
    lv_obj_remove_flag(line, LV_OBJ_FLAG_HIDDEN);
    if (lv_anim_get(line, weather_loading_move)) return;
    lv_anim_t animation;
    lv_anim_init(&animation);
    lv_anim_set_var(&animation, line);
    lv_anim_set_exec_cb(&animation, weather_loading_move);
    lv_anim_set_values(&animation, -76, 320);
    lv_anim_set_duration(&animation, 900);
    lv_anim_set_repeat_count(&animation, LV_ANIM_REPEAT_INFINITE);
    lv_anim_start(&animation);
}
