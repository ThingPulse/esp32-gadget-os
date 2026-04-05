#pragma once

#include <lvgl.h>

enum class WeatherIconKind {
    Clear,
    Cloudy,
    Rain,
    Snow,
    Storm,
    Fog,
};

WeatherIconKind weather_icon_kind_from_code(int weather_code);

// Creates a reusable icon object. Use weather_icon_update() to change type/size.
lv_obj_t *weather_icon_create(lv_obj_t *parent, WeatherIconKind kind, lv_coord_t size);
void weather_icon_update(lv_obj_t *icon_root, WeatherIconKind kind, lv_coord_t size);
