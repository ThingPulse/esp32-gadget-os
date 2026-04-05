#pragma once

#include <lvgl.h>

lv_obj_t *create_wifi_status_icon(lv_obj_t *parent, lv_coord_t x_offset = -8);
lv_obj_t *create_time_status_label(lv_obj_t *parent, lv_coord_t x_offset = 8);