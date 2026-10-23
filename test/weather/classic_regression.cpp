#include <string>
#include <cassert>
#include <cstring>
#define private public
#include "services/WeatherService.h"
#include "services/TimeService.h"
#undef private
#include "services/WeatherLocationStore.h"
#include "../../src/ui/ClassicWeatherView.cpp"

int main() {
    // Reference values from SunCalc 1.9.0 at 2026-10-21 18:00 UTC.
    // Phase is global; the bright limb must change with latitude, longitude and time.
    const time_t instant = 1792605600;
    const auto zurich = moon_appearance::calculate(instant, 47.4, 8.5);
    const auto sydney = moon_appearance::calculate(instant, -33.9, 151.2);
    const auto equator = moon_appearance::calculate(instant, 0, 8.5);
    const auto east = moon_appearance::calculate(instant, 47.4, 90);
    const auto later = moon_appearance::calculate(instant + 14400, 47.4, 8.5);
    assert(std::abs(zurich.phase - .34742007962012394) < 1e-8);
    assert(std::abs(zurich.illumination - .7872971709777719) < 1e-8);
    assert(zurich.illumination == sydney.illumination);
    assert(std::abs(zurich.bright_angle + 1.577108145111366) < 1e-8);
    assert(std::abs(sydney.bright_angle + 4.183779273986425) < 1e-8);
    assert(std::abs(equator.bright_angle + .6760609141394065) < 1e-8);
    assert(std::abs(east.bright_angle + 2.5385417531391252) < 1e-8);
    assert(std::abs(later.bright_angle + 2.342840228624846) < 1e-8);
    for (double latitude : {-90., 90.})
        assert(std::isfinite(moon_appearance::calculate(instant, latitude, 0).bright_angle));
    lv_init();
    auto *display = lv_display_create(320, 480);
    assert(display);
    ClassicWeatherView view;
    classic_weather_create(view, lv_screen_active());
    auto &weather = WeatherService::instance();
    auto &clock = TimeService::instance();
    tm local{}, device_before{}, device_after{};
    localtime_r(&instant, &device_before);
    auto set_offset = [&](int offset) {
        std::string payload = "{\"utc_offset_seconds\":" + std::to_string(offset) +
            ",\"current\":{\"temperature_2m\":14}}";
        assert(weather.apply_payload(payload));
        assert(weather.local_time(instant, local));
    };
    set_offset(7200); assert(local.tm_hour == 20 && local.tm_mday == 21);
    set_offset(-14400); assert(local.tm_hour == 14 && local.tm_mday == 21);
    set_offset(20700); assert(local.tm_hour == 23 && local.tm_min == 45);
    set_offset(-12600); assert(local.tm_hour == 14 && local.tm_min == 30);
    set_offset(50400); assert(local.tm_hour == 8 && local.tm_mday == 22);
    set_offset(3600); assert(local.tm_hour == 19); // Refreshed winter offset.
    localtime_r(&instant, &device_after);
    assert(device_before.tm_hour == device_after.tm_hour && device_before.tm_mday == device_after.tm_mday);
    std::string missing_offset = "{\"current\":{\"temperature_2m\":14}}";
    assert(weather.apply_payload(missing_offset));
    assert(!weather.local_time(instant, local));
    std::vector<WeatherLocation> locations = {
        {"Zurich", "", "", "Europe/Zurich", 47.4, 8.5},
        {"New York", "", "", "America/New_York", 40.7, -74.0}};
    assert(WeatherLocationStore::save(locations, 0));
    assert(weather.reload_from_config());
    set_offset(7200);
    assert(WeatherLocationStore::save(locations, 1));
    assert(weather.reload_from_config());
    assert(!weather.local_time(instant, local)); // Never reuse the previous location's offset.
    set_offset(0);

    clock._synchronized = false;
    classic_weather_tick(view);
    assert(std::strcmp(lv_label_get_text(view.clock), "--:--") == 0);
    clock._synchronized = true;
    classic_weather_tick(view);
    assert(std::strcmp(lv_label_get_text(view.clock), "--:--") != 0);
    set_offset(20700);
    classic_weather_tick(view); // A new offset must invalidate the clock within the same second.
    tm expected{};
    assert(weather.local_time(time(nullptr), expected));
    char expected_clock[16];
    strftime(expected_clock, sizeof(expected_clock), "%H:%M", &expected);
    assert(std::strcmp(lv_label_get_text(view.clock), expected_clock) == 0);
    clock._use_24h = false;
    classic_weather_tick(view);
    assert(std::strstr(lv_label_get_text(view.seconds), "AM") ||
           std::strstr(lv_label_get_text(view.seconds), "PM"));
    clock._use_24h = true;
    classic_weather_tick(view);
    weather._has_weather = true;
    weather._temperature = 14.3f;
    weather._wind_speed = 4;
    weather._moon_phase = .25f;
    classic_weather_update(view);
    assert(std::strcmp(lv_label_get_text(view.temperature), "14.3°") == 0);
    assert(std::strcmp(lv_label_get_text(view.wind), "4.0 km/h") == 0);
    assert(!lv_obj_has_flag(view.moon, LV_OBJ_FLAG_HIDDEN));
    shade_moon(view, .25f);
    const auto first = view.moon_pixels;
    shade_moon(view, .75f);
    const int left = (36 * 72 + 18) * 4;
    const int right = (36 * 72 + 54) * 4;
    assert(first[left] < view.moon_pixels[left]);
    assert(first[right] > view.moon_pixels[right]);
    assert(view.moon_pixels[3] == 0); // Transparent outside the lunar disc.
    shade_moon(view, 0);
    const auto dark = view.moon_pixels;
    shade_moon(view, .5f);
    assert(view.moon_pixels[left] > dark[left]);
    assert(view.moon_pixels[right] > dark[right]);
    shade_moon(view, .25f);
    // Several intermediate brightness levels replace the former binary shadow.
    auto brightness = [&](int x) {
        const int pixel = 36 * 72 + x;
        return double(view.moon_pixels[pixel * 4]) / moon_texture[pixel * 3 + 2];
    };
    assert(brightness(35) < brightness(37));
    assert(brightness(37) < brightness(39));
    assert(brightness(39) < brightness(42));
    bool has_soft_edge = false;
    for (size_t i = 3; i < view.moon_pixels.size(); i += 4)
        has_soft_edge |= view.moon_pixels[i] > 0 && view.moon_pixels[i] < 255;
    assert(has_soft_edge);
    // Negative bright-limb angle lights the right side in screen coordinates.
    shade_moon(view, .25f, -PI / 2, 0, true);
    assert(brightness(54) > brightness(18));
    shade_moon(view, .25f, PI / 2, 0, true);
    assert(brightness(18) > brightness(54));
    weather._latitude = 47.4; weather._longitude = 8.5;
    update_moon(view, instant);
    const auto north = view.moon_pixels;
    weather._latitude = -33.9; weather._longitude = 151.2;
    update_moon(view, instant);
    assert(view.moon_pixels != north);
    assert(std::strstr(lv_label_get_text(view.phase), "79% lit"));
    clock._synchronized = false;
    update_moon(view, instant);
    assert(view.last_moon_minute == -1);
    assert(std::strstr(lv_label_get_text(view.phase), "50% lit"));
    clock._synchronized = true;
    view.last_clock.clear();
    classic_weather_tick(view);
    assert(view.last_moon_minute >= 0);
    weather._pressure = 1013.25f;
    weather._metric = true;
    classic_weather_update(view);
    assert(std::strcmp(lv_label_get_text(view.pressure), "1013 hPa") == 0);
    weather._metric = false;
    classic_weather_update(view);
    assert(std::strcmp(lv_label_get_text(view.pressure), "29.92 inHg") == 0);
    assert(std::abs(weather._pressure - 1013.25f) < .001f); // Keep cached source units unchanged.
    assert(std::strcmp(lv_label_get_text(view.wind), "4.0 mph") == 0);
    weather._hourly_forecast_count = 2;
    weather._hourly_forecast[0] = {true, "2026-10-21T23:00", 3, 54};
    weather._hourly_forecast[1] = {true, "2026-10-22T00:00", 61, 52};
    lv_obj_send_event(view.daily_row, LV_EVENT_CLICKED, nullptr);
    assert(view.hourly_overlay && view.hourly_list);
    // Each date heading is followed by its hourly row, including across midnight.
    assert(lv_obj_get_child_count(view.hourly_list) == 4);
    auto *first_hour = lv_obj_get_child(view.hourly_list, 1);
    assert(std::strcmp(lv_label_get_text(lv_obj_get_child(first_hour, 0)), "23:00") == 0);
    assert(std::strcmp(lv_label_get_text(lv_obj_get_child(first_hour, 3)), "54°F") == 0);
    auto *overlay = view.hourly_overlay;
    show_hourly(view);
    assert(view.hourly_overlay == overlay); // Repeated taps cannot stack dialogs.
    clock._use_24h = false;
    populate_hourly(view);
    first_hour = lv_obj_get_child(view.hourly_list, 1);
    assert(std::strcmp(lv_label_get_text(lv_obj_get_child(first_hour, 0)), "11:00 PM") == 0);
    weather._hourly_forecast_count = 0;
    classic_weather_update(view);
    assert(lv_obj_get_child_count(view.hourly_list) == 1);
    assert(std::strcmp(lv_label_get_text(lv_obj_get_child(view.hourly_list, 0)), "Hourly forecast unavailable") == 0);
    lv_obj_delete(view.hourly_overlay);
    assert(!view.hourly_overlay && !view.hourly_list);
    show_hourly(view);
    assert(view.hourly_overlay);
    lv_obj_delete(view.hourly_overlay);
    weather._has_weather = false;
    classic_weather_update(view);
    assert(lv_obj_has_flag(view.moon, LV_OBJ_FLAG_HIDDEN));
    assert(lv_obj_has_flag(view.icon, LV_OBJ_FLAG_HIDDEN));
    assert(std::strcmp(lv_label_get_text(view.temperature), "--") == 0);
    assert(std::strcmp(lv_label_get_text(view.clock), "--:--") != 0);
    show_hourly(view);
    lv_obj_clean(lv_screen_active()); // Leaving Weather also removes the modal.
    assert(!view.hourly_overlay && !view.hourly_list);
    puts("Classic weather: labels, clock, lunar phases, observer orientation, soft shading, hourly overlay, and stale-data clearing passed");
}
