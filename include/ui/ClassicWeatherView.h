#pragma once
#include <lvgl.h>
#include <array>
#include <cstdint>
#include <string>
#include <vector>

struct ClassicWeatherView {
    lv_obj_t *date = nullptr, *clock = nullptr, *seconds = nullptr;
    lv_obj_t *condition = nullptr, *temperature = nullptr, *humidity = nullptr;
    lv_obj_t *pressure = nullptr, *wind = nullptr, *wind_arrow = nullptr;
    lv_obj_t *icon = nullptr, *sun = nullptr, *phase = nullptr, *moon = nullptr;
    lv_obj_t *daily_row = nullptr, *hourly_overlay = nullptr, *hourly_list = nullptr;
    std::array<lv_obj_t *, 4> days{}, icons{}, ranges{};
    lv_point_precise_t arrow_points[5]{};
    lv_image_dsc_t moon_image{};
    std::vector<uint8_t> moon_pixels;
    std::string last_clock;
    int64_t last_moon_minute = -1;
};

void classic_weather_create(ClassicWeatherView &view, lv_obj_t *parent);
void classic_weather_update(ClassicWeatherView &view);
void classic_weather_tick(ClassicWeatherView &view);
