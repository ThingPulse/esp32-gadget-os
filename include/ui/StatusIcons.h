#pragma once

#include <lvgl.h>

lv_obj_t *create_wifi_status_icon(lv_obj_t *parent, lv_coord_t x_offset = -8);
lv_obj_t *create_time_status_label(lv_obj_t *parent, lv_coord_t x_offset = 8);
lv_obj_t *create_weather_center_label(lv_obj_t *parent, const char *fallback_text, lv_coord_t x_offset = 0);