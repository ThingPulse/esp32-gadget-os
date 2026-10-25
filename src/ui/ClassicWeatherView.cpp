#include "ui/I18n.h"
#include "ui/ClassicWeatherView.h"
#include "ui/WeatherIconSet.h"
#include "ui/MoonAppearance.h"
#include "services/WeatherService.h"
#include "services/TimeService.h"
#include "assets/MoonTextureData.inc"
#include "assets/WeatherFonts.h"
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <ctime>

namespace {
constexpr double PI = 3.14159265358979323846;
void textf(lv_obj_t *obj, const char *format, ...) {
    // LVGL's compact formatter is built without floating-point support.
    char text[128];
    va_list args;
    va_start(args, format);
    std::vsnprintf(text, sizeof(text), format, args);
    va_end(args);
    lv_label_set_text(obj, text);
}
lv_obj_t *label(lv_obj_t *parent, int x, int y, int width, const char *text,
                const lv_font_t *font = i18n::font14()) {
    auto *obj = lv_label_create(parent);
    lv_obj_set_pos(obj, x, y);
    lv_obj_set_width(obj, width);
    lv_obj_set_style_text_font(obj, font, 0);
    lv_obj_set_style_text_color(obj, lv_color_hex(0xe7eaf4), 0);
    lv_obj_set_style_text_align(obj, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(obj, text);
    return obj;
}
const char *condition(int code) {
    if (code == 0) return i18n::tr("Clear sky");
    if (code <= 3) return i18n::tr("Cloudy");
    if (code <= 48) return i18n::tr("Fog");
    if (code <= 67) return i18n::tr("Rain");
    if (code <= 77) return i18n::tr("Snow");
    if (code <= 82) return i18n::tr("Showers");
    if (code >= 95) return i18n::tr("Thunderstorm");
    return i18n::tr("Unknown");
}
std::string hour(const std::string &iso) {
    return iso.size() >= 16 ? iso.substr(11, 5) : "--:--";
}
void plain_container(lv_obj_t *obj) {
    lv_obj_set_style_pad_all(obj, 0, 0);
    lv_obj_set_style_border_width(obj, 0, 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_TRANSP, 0);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
}
void populate_hourly(ClassicWeatherView &v) {
    if (!v.hourly_list) return;
    const int scroll_y = lv_obj_get_scroll_y(v.hourly_list);
    lv_obj_clean(v.hourly_list);
    const auto &weather = WeatherService::instance();
    const auto *hours = weather.forecast_hours();
    int shown = 0;
    std::string date;
    for (int i = 0; weather.has_weather() && i < weather.hourly_forecast_count(); ++i) {
        if (!hours[i].valid) continue;
        const auto next_date = hours[i].time.substr(0, 10);
        if (next_date != date) {
            date = next_date;
            auto *day = label(v.hourly_list, 0, 0, 240, date.c_str());
            lv_obj_set_width(day, lv_pct(100));
            lv_obj_set_style_text_color(day, lv_color_hex(0x9aa5bd), 0);
        }
        auto *row = lv_obj_create(v.hourly_list);
        plain_container(row);
        lv_obj_set_size(row, lv_pct(100), 42);
        lv_obj_clear_flag(row, LV_OBJ_FLAG_CLICKABLE);
        auto *time_label = label(row, 0, 10, 70, hour(hours[i].time).c_str());
        if (!TimeService::instance().use_24h() && hours[i].time.size() >= 16) {
            int h = 0, m = 0;
            if (std::sscanf(hours[i].time.c_str() + 11, "%d:%d", &h, &m) == 2)
                textf(time_label, "%d:%02d %s", h % 12 ? h % 12 : 12, m, h < 12 ? "AM" : "PM");
        }
        auto *icon = weather_icon_create_lightweight(row, weather_icon_kind_from_code(hours[i].weather_code), 30);
        lv_obj_set_pos(icon, 78, 4);
        lv_obj_clear_flag(icon, LV_OBJ_FLAG_CLICKABLE);
        auto *description = label(row, 114, 10, 66, condition(hours[i].weather_code));
        lv_label_set_long_mode(description, LV_LABEL_LONG_DOT);
        auto *temperature = label(row, 0, 0, 62, "");
        textf(temperature, "%.0f°%s", hours[i].temperature, weather.is_metric() ? "C" : "F");
        lv_obj_align(temperature, LV_ALIGN_RIGHT_MID, -2, 0);
        ++shown;
    }
    if (!shown) {
        auto *empty = label(v.hourly_list, 0, 0, 240, i18n::tr("Hourly forecast unavailable"));
        lv_obj_set_width(empty, lv_pct(100));
    }
    lv_obj_update_layout(v.hourly_list);
    lv_obj_scroll_to_y(v.hourly_list, scroll_y, LV_ANIM_OFF);
}
void show_hourly(ClassicWeatherView &v) {
    if (v.hourly_overlay) return;
    auto *screen = lv_obj_get_screen(v.daily_row);
    auto *overlay = lv_obj_create(screen);
    v.hourly_overlay = overlay;
    plain_container(overlay);
    lv_obj_set_size(overlay, lv_pct(100), lv_pct(100));
    lv_obj_set_style_radius(overlay, 0, 0);
    lv_obj_set_style_bg_color(overlay, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(overlay, LV_OPA_60, 0);
    lv_obj_clear_flag(overlay, static_cast<lv_obj_flag_t>(LV_OBJ_FLAG_SCROLL_CHAIN | LV_OBJ_FLAG_GESTURE_BUBBLE));
    lv_obj_add_event_cb(overlay, [](lv_event_t *event) {
        auto &view = *static_cast<ClassicWeatherView *>(lv_event_get_user_data(event));
        view.hourly_overlay = nullptr;
        view.hourly_list = nullptr;
    }, LV_EVENT_DELETE, &v);
    auto *dialog = lv_obj_create(overlay);
    lv_obj_set_size(dialog, 296, std::min(380, int(lv_obj_get_height(screen)) - 24));
    lv_obj_center(dialog);
    lv_obj_set_style_pad_all(dialog, 12, 0);
    lv_obj_set_style_pad_row(dialog, 8, 0);
    lv_obj_set_style_border_width(dialog, 0, 0);
    lv_obj_set_style_bg_color(dialog, lv_color_hex(0x2a2d42), 0);
    lv_obj_clear_flag(dialog, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(dialog, LV_FLEX_FLOW_COLUMN);
    auto *title = label(dialog, 0, 0, 272, i18n::tr("Hourly forecast"), i18n::font24());
    lv_obj_set_width(title, lv_pct(100));
    auto *subtitle = label(dialog, 0, 0, 272, i18n::tr("Upcoming hours (local time)"));
    lv_obj_set_width(subtitle, lv_pct(100));
    v.hourly_list = lv_obj_create(dialog);
    plain_container(v.hourly_list);
    lv_obj_set_width(v.hourly_list, lv_pct(100));
    lv_obj_set_flex_grow(v.hourly_list, 1);
    lv_obj_set_flex_flow(v.hourly_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(v.hourly_list, 4, 0);
    lv_obj_add_flag(v.hourly_list, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(v.hourly_list, LV_DIR_VER);
    lv_obj_clear_flag(v.hourly_list, static_cast<lv_obj_flag_t>(LV_OBJ_FLAG_SCROLL_CHAIN | LV_OBJ_FLAG_GESTURE_BUBBLE));
    lv_obj_set_scrollbar_mode(v.hourly_list, LV_SCROLLBAR_MODE_AUTO);
    auto *close = lv_button_create(dialog);
    lv_obj_set_size(close, lv_pct(100), 40);
    auto *close_label = lv_label_create(close);
    lv_label_set_text(close_label, i18n::tr("Close"));
    lv_obj_center(close_label);
    lv_obj_add_event_cb(close, [](lv_event_t *event) {
        auto *overlay = static_cast<lv_obj_t *>(lv_event_get_user_data(event));
        lv_obj_delete_async(overlay);
    }, LV_EVENT_CLICKED, overlay);
    populate_hourly(v);
}
// Bilinear texture sampling avoids jagged craters when the observer view rotates.
double moon_texture_sample(double x, double y, int channel) {
    x = std::max(0.0, std::min(71.0, x));
    y = std::max(0.0, std::min(71.0, y));
    const int x0 = int(x), y0 = int(y), x1 = std::min(71, x0 + 1), y1 = std::min(71, y0 + 1);
    const double fx = x - x0, fy = y - y0;
    auto sample = [&](int sx, int sy) { return moon_texture[(sy * 72 + sx) * 3 + channel]; };
    return (sample(x0, y0) * (1 - fx) + sample(x1, y0) * fx) * (1 - fy) +
           (sample(x0, y1) * (1 - fx) + sample(x1, y1) * fx) * fy;
}
void shade_moon(ClassicWeatherView &v, float phase, double bright_angle = 0,
                double rotation = 0, bool oriented = false) {
    const double lz = -std::cos(phase * 2 * PI);
    const double lateral = std::abs(std::sin(phase * 2 * PI));
    const double lx = oriented ? -std::sin(bright_angle) * lateral : std::sin(phase * 2 * PI);
    const double ly = oriented ? -std::cos(bright_angle) * lateral : 0;
    const double cr = std::cos(rotation), sr = std::sin(rotation);
    for (int y = 0; y < 72; ++y) for (int x = 0; x < 72; ++x) {
        const double nx = (x - 35.5) / 35.5, ny = (y - 35.5) / 35.5;
        const size_t i = y * 72 + x;
        const double radius = std::sqrt(nx * nx + ny * ny);
        const double alpha = std::max(0.0, std::min(1.0, (1 - radius) * 35.5 + .5));
        const double nz = std::sqrt(std::max(0.0, 1 - nx * nx - ny * ny));
        const double light = nx * lx + ny * ly + nz * lz;
        // Continuous diffuse light with a narrow, smooth terminator transition.
        // A faint earthshine floor preserves texture on the unlit side.
        const double t = std::max(0.0, std::min(1.0, (light + .04) / .22));
        const double brightness = .10 + .90 * t * t * (3 - 2 * t);
        const double tx = 35.5 + 35.5 * (cr * nx + sr * ny);
        const double ty = 35.5 + 35.5 * (-sr * nx + cr * ny);
        // LVGL ARGB8888 uses BGRA byte order on our little-endian targets.
        for (int c = 0; c < 3; ++c)
            v.moon_pixels[i * 4 + c] = (uint8_t)(moon_texture_sample(tx, ty, 2 - c) * brightness);
        v.moon_pixels[i * 4 + 3] = (uint8_t)(255 * alpha);
    }
    lv_obj_invalidate(v.moon);
}
void update_moon(ClassicWeatherView &v, time_t now) {
    auto &weather = WeatherService::instance();
    if (!weather.has_weather()) return;
    if (TimeService::instance().is_synchronized()) {
        const auto appearance = moon_appearance::calculate(now, weather.latitude(), weather.longitude());
        textf(v.phase, i18n::tr("%s\n%.0f%% lit"), i18n::tr(moon_appearance::phase_name(appearance.phase)), appearance.illumination * 100);
        shade_moon(v, appearance.phase, appearance.bright_angle, appearance.parallactic_angle, true);
    } else {
        // No trusted clock yet: retain the date-based phase, without inventing a local tilt.
        textf(v.phase, i18n::tr("%s\n%.0f%% lit"), i18n::tr(weather.moon_phase_name()), weather.moon_illumination_percent());
        shade_moon(v, weather.moon_phase_fraction());
    }
    v.last_moon_minute = TimeService::instance().is_synchronized() ? now / 60 : -1;
    lv_obj_clear_flag(v.moon, LV_OBJ_FLAG_HIDDEN);
}
}

void classic_weather_create(ClassicWeatherView &v, lv_obj_t *parent) {
    // A single 320px dashboard fits portrait; landscape can scroll vertically.
    auto *root = lv_obj_create(parent);
    lv_obj_set_size(root, 320, 396);
    lv_obj_align(root, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_clear_flag(root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(root, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(root, 0, 0);
    lv_obj_set_style_pad_all(root, 0, 0);
    for (int y : {74, 194, 298}) {
        auto *line = lv_obj_create(root);
        lv_obj_set_pos(line, 12, y);
        lv_obj_set_size(line, 296, 1);
        lv_obj_set_style_border_width(line, 0, 0);
        lv_obj_set_style_bg_color(line, lv_color_hex(0x454d62), 0);
        lv_obj_clear_flag(line, LV_OBJ_FLAG_SCROLLABLE);
    }
    v.date = label(root, 0, 0, 320, "--, --.--.----");
    v.clock = label(root, 30, 28, 244, "--:--", &weather_clock_48);
    v.seconds = label(root, 210, 40, 52, "--");
    // Keep all condition readings on the same column centre.
    constexpr int readingsX=72, readingsWidth=152;
    v.condition = label(root, readingsX, 78, readingsWidth, i18n::tr("No weather data"));
    v.icon = weather_icon_create(root, WeatherIconKind::Cloudy, 56);
    lv_obj_set_pos(v.icon, 10, 108);
    v.temperature = label(root, readingsX, 110, readingsWidth, "--", &weather_temperature_36);
    v.humidity = label(root, readingsX, 146, readingsWidth, "--");
    v.pressure = label(root, readingsX, 167, readingsWidth, "--");
    auto *compass = lv_obj_create(root);
    lv_obj_set_pos(compass, 244, 100);
    lv_obj_set_size(compass, 60, 60);
    lv_obj_set_style_bg_opa(compass, LV_OPA_TRANSP, 0);
    lv_obj_set_style_radius(compass, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_color(compass, lv_color_hex(0x697797), 0);
    lv_obj_set_style_border_width(compass, 1, 0);
    lv_obj_set_style_pad_all(compass, 0, 0);
    lv_obj_clear_flag(compass, LV_OBJ_FLAG_SCROLLABLE);
    label(compass, 18, 0, 22, "N");
    v.wind_arrow = lv_line_create(compass);
    lv_obj_set_style_line_color(v.wind_arrow, lv_color_hex(0xff6b7a), 0);
    lv_obj_set_style_line_width(v.wind_arrow, 3, 0);
    v.wind = label(root, 229, 168, 90, "--");
    // Keep the existing daily section footprint; the small hint sits above the days.
    v.daily_row = lv_obj_create(root);
    plain_container(v.daily_row);
    lv_obj_set_pos(v.daily_row, 0, 194);
    lv_obj_set_size(v.daily_row, 320, 104);
    lv_obj_set_style_bg_color(v.daily_row, lv_color_hex(0x39445b), LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(v.daily_row, LV_OPA_50, LV_STATE_PRESSED);
    lv_obj_add_event_cb(v.daily_row, [](lv_event_t *event) {
        show_hourly(*static_cast<ClassicWeatherView *>(lv_event_get_user_data(event)));
    }, LV_EVENT_CLICKED, &v);
    auto *hint = label(v.daily_row, 228, 0, 80, (std::string(i18n::tr("Hourly "))+LV_SYMBOL_RIGHT).c_str());
    lv_obj_set_style_text_color(hint, lv_color_hex(0x63e6be), 0);
    for (int i = 0; i < 4; ++i) {
        v.days[i] = label(v.daily_row, i * 80, 18, 80, "--");
        v.icons[i] = weather_icon_create_lightweight(v.daily_row, WeatherIconKind::Cloudy, 40);
        lv_obj_set_pos(v.icons[i], i * 80 + 20, 37);
        lv_obj_clear_flag(v.icons[i], LV_OBJ_FLAG_CLICKABLE);
        v.ranges[i] = label(v.daily_row, i * 80, 82, 80, "--");
    }
    label(root, 0, 311, 110, i18n::tr("Sun"), i18n::font24());
    v.sun = label(root, 0, 342, 110, i18n::tr("Rise --:--\nSet --:--"));
    v.moon_pixels.resize(72 * 72 * 4);
    v.moon_image.header.magic = LV_IMAGE_HEADER_MAGIC;
    v.moon_image.header.cf = LV_COLOR_FORMAT_ARGB8888;
    v.moon_image.header.w = v.moon_image.header.h = 72;
    v.moon_image.header.stride = 72 * 4;
    v.moon_image.data_size = v.moon_pixels.size();
    v.moon_image.data = v.moon_pixels.data();
    v.moon = lv_image_create(root);
    lv_image_set_src(v.moon, &v.moon_image);
    lv_obj_set_pos(v.moon, 122, 310);
    label(root, 204, 311, 112, i18n::tr("Moon"), i18n::font24());
    v.phase = label(root, 204, 342, 112, i18n::tr("Unavailable"));
    lv_obj_add_flag(v.moon, LV_OBJ_FLAG_HIDDEN);
    classic_weather_tick(v);
}

void classic_weather_tick(ClassicWeatherView &v) {
    if (!v.clock) return;
    const bool synced = TimeService::instance().is_synchronized();
    const time_t now = time(nullptr);
    tm local{};
    const bool local_known = WeatherService::instance().local_time(now, local);
    const int local_second = local_known ? local.tm_yday * 86400 + local.tm_hour * 3600 +
                                           local.tm_min * 60 + local.tm_sec : -1;
    const std::string key = std::to_string(local_second) + ":" + std::to_string(now) + (synced ? "s" : "u") +
                            (TimeService::instance().use_24h() ? "24" : "12");
    if (key == v.last_clock) return;
    v.last_clock = key;
    if (!synced || !local_known) {
        lv_label_set_text(v.clock, "--:--");
        lv_label_set_text(v.seconds, "--");
        lv_label_set_text(v.date, synced ? i18n::tr("Waiting for location time") : i18n::tr("Waiting for time"));
        return;
    }
    if (v.last_moon_minute != now / 60) update_moon(v, now);

    char text[64];
    i18n::date(text, sizeof(text), local);
    lv_label_set_text(v.date, text);
    strftime(text, sizeof(text), TimeService::instance().use_24h() ? "%H:%M" : "%I:%M", &local);
    lv_label_set_text(v.clock, text);
    strftime(text, sizeof(text), TimeService::instance().use_24h() ? "%S" : "%S\n%p", &local);
    lv_label_set_text(v.seconds, text);
}

void classic_weather_update(ClassicWeatherView &v) {
    auto &weather = WeatherService::instance();
    const bool valid = weather.has_weather();
    populate_hourly(v);
    lv_label_set_text(v.condition, valid ? condition(weather.weather_code()) : i18n::tr("No weather data"));
    if (valid) {
        lv_obj_clear_flag(v.icon, LV_OBJ_FLAG_HIDDEN);
        weather_icon_update(v.icon, weather_icon_kind_from_code(weather.weather_code()), 56);
        textf(v.temperature, "%.1f°", weather.temperature_value());
        textf(v.humidity, i18n::tr("%.0f%% humidity"), weather.humidity_value());
        textf(v.pressure, weather.is_metric() ? "%.0f hPa" : "%.2f inHg", weather.pressure_value());
        textf(v.wind, "%.1f %s", weather.wind_speed_value(), weather.is_metric() ? "km/h" : "mph");
        // Meteorological bearings describe where wind comes FROM.
        // The arrowhead shows airflow, so point toward the opposite bearing.
        const double a = (weather.wind_direction_degrees() + 180.0) * PI / 180;
        auto point = [&](double x, double y) { return lv_point_precise_t{
            (lv_value_precise_t)(29 + x * std::cos(a) - y * std::sin(a)),
            (lv_value_precise_t)(29 + x * std::sin(a) + y * std::cos(a))}; };
        v.arrow_points[0] = point(0, 17); v.arrow_points[1] = point(0, -17);
        v.arrow_points[2] = point(-7, -7); v.arrow_points[3] = point(0, -17);
        v.arrow_points[4] = point(7, -7);
        lv_line_set_points(v.wind_arrow, v.arrow_points, 5);
        lv_obj_clear_flag(v.wind_arrow, LV_OBJ_FLAG_HIDDEN);
        textf(v.sun, i18n::tr("Rise %s\nSet %s"), hour(weather.sunrise_time()).c_str(), hour(weather.sunset_time()).c_str());
        update_moon(v, time(nullptr));
    } else {
        lv_obj_add_flag(v.icon, LV_OBJ_FLAG_HIDDEN);
        for (auto *obj : {v.temperature, v.humidity, v.pressure, v.wind}) lv_label_set_text(obj, "--");
        lv_label_set_text(v.sun, i18n::tr("Rise --:--\nSet --:--"));
        lv_label_set_text(v.phase, i18n::tr("Unavailable"));
        lv_obj_add_flag(v.wind_arrow, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(v.moon, LV_OBJ_FLAG_HIDDEN);
    }
    for (int i = 0; i < 4; ++i) {
        const auto &day = weather.forecast_days()[i];
        const bool show = valid && i < weather.forecast_count() && day.valid;
        if (!show) {
            lv_label_set_text(v.days[i], "--"); lv_label_set_text(v.ranges[i], "--");
            lv_obj_add_flag(v.icons[i], LV_OBJ_FLAG_HIDDEN);
            continue;
        }
        tm date{};
        int year, month, d;
        char name[8] = "--";
        if (std::sscanf(day.date.c_str(), "%d-%d-%d", &year, &month, &d) == 3) {
            date.tm_year = year - 1900; date.tm_mon = month - 1; date.tm_mday = d;
            date.tm_hour = 12; date.tm_isdst = -1;
            mktime(&date); std::snprintf(name,sizeof(name),"%s",i18n::weekday(date.tm_wday,true));
        }
        lv_label_set_text(v.days[i], name);
        textf(v.ranges[i], "%.0f-%.0f°", day.temp_min, day.temp_max);
        weather_icon_update_lightweight(v.icons[i], weather_icon_kind_from_code(day.weather_code), 40);
        lv_obj_clear_flag(v.icons[i], LV_OBJ_FLAG_HIDDEN);
    }
}
