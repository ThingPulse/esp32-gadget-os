#include "apps/WeatherApp.h"

#include "AppRegistry.h"
#include "services/WeatherService.h"
#include "services/WeatherLocationStore.h"
#include "ui/UiNav.h"
#include "ui/StatusIcons.h"
#include "ui/UiSessionState.h"
#include "ui/UiTrace.h"
#include "ui/WeatherIconSet.h"

#include <cmath>
#include <cstdio>
#include <ctime>
#include <string>
#include <vector>

APP_REGISTER_COLOR_RANK("Weather", LV_SYMBOL_EYE_OPEN, weather_app_launch, 0x339af0, 10);

namespace {

struct WeatherAppCtx {
  lv_obj_t *screen = nullptr;
  lv_obj_t *date_label = nullptr;
  lv_obj_t *current_icon = nullptr;
  lv_obj_t *condition_label = nullptr;
  lv_obj_t *temp_label = nullptr;
  lv_obj_t *humidity_label = nullptr;
  lv_obj_t *pressure_label = nullptr;
  lv_obj_t *precip_label = nullptr;
  lv_obj_t *wind_label = nullptr;
  lv_obj_t *hourly_rows[WeatherService::FORECAST_HOURS]{};
  lv_obj_t *hourly_time_labels[WeatherService::FORECAST_HOURS]{};
  lv_obj_t *hourly_icons[WeatherService::FORECAST_HOURS]{};
  lv_obj_t *hourly_temp_labels[WeatherService::FORECAST_HOURS]{};
  lv_obj_t *forecast_rows[WeatherService::FORECAST_DAYS]{};
  lv_obj_t *forecast_day_labels[WeatherService::FORECAST_DAYS]{};
  lv_obj_t *forecast_icons[WeatherService::FORECAST_DAYS]{};
  lv_obj_t *forecast_temp_labels[WeatherService::FORECAST_DAYS]{};
  lv_obj_t *wind_card_speed_label = nullptr;
  lv_obj_t *wind_card_dir_label = nullptr;
  lv_obj_t *wind_rose_marker = nullptr;
  lv_obj_t *refresh_btn = nullptr;
  lv_obj_t *loading_line = nullptr;
  lv_timer_t *refresh_timer = nullptr;
  unsigned long weather_revision = 0;
  bool was_fetching = false;
  bool compact_list_icons = false;
  bool closing = false;
};

static WeatherAppCtx app_ctx;

static const char *weather_text_for_code(int weather_code)
{
  if (weather_code == 0) return "Clear";
  if (weather_code <= 3) return "Cloudy";
  if (weather_code >= 45 && weather_code <= 48) return "Fog";
  if (weather_code >= 51 && weather_code <= 67) return "Rain";
  if (weather_code >= 71 && weather_code <= 77) return "Snow";
  if (weather_code >= 80 && weather_code <= 82) return "Showers";
  if (weather_code >= 95) return "Thunderstorm";
  return "Unknown";
}

static const char *wind_direction_short(float degrees)
{
  static const char *dirs[] = {"N", "NE", "E", "SE", "S", "SW", "W", "NW"};
  int index = (int)((degrees + 22.5f) / 45.0f) % 8;
  if (index < 0) index += 8;
  return dirs[index];
}

static void update_wind_rose_marker(lv_obj_t *marker, float degrees)
{
  if (!marker || !lv_obj_is_valid(marker)) return;

  float normalized = std::fmod(degrees, 360.0f);
  if (normalized < 0.0f) normalized += 360.0f;

  const float rad = normalized * (3.14159265f / 180.0f);
  const int center = 36;
  const int radius = 23;
  const int marker_r = 4;
  const int x = center + (int)std::lround(std::sin(rad) * radius);
  const int y = center - (int)std::lround(std::cos(rad) * radius);
  lv_obj_set_pos(marker, x - marker_r, y - marker_r);
}

static std::string short_date(const std::string &iso)
{
  if (iso.size() >= 10) return iso.substr(5, 5);
  return iso;
}

static int weekday_from_ymd(int y, int m, int d)
{
  if (m < 3) {
    m += 12;
    y -= 1;
  }
  int k = y % 100;
  int j = y / 100;
  int h = (d + (13 * (m + 1)) / 5 + k + k / 4 + j / 4 + (5 * j)) % 7;
  return (h + 6) % 7;  // 0=Sun .. 6=Sat
}

static std::string weekday_label_from_iso(const std::string &iso)
{
  if (iso.size() < 10) return iso;

  int y = 0;
  int m = 0;
  int d = 0;
  if (std::sscanf(iso.c_str(), "%d-%d-%d", &y, &m, &d) != 3)
    return short_date(iso);

  static const char *names[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
  int w = weekday_from_ymd(y, m, d);
  if (w < 0 || w > 6) return short_date(iso);

  return names[w];
}

static std::string hour_label_from_iso(const std::string &iso)
{
  if (iso.size() >= 16) return iso.substr(11, 5);
  if (iso.size() >= 13) return iso.substr(11, 2) + std::string(":00");
  return "--:--";
}

static void nav_back_event(lv_event_t *e)
{
  if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
    UI_TRACE("Weather", "back clicked");
    WeatherService::instance().pause();
    app_ctx.closing = true;
    if (app_ctx.refresh_timer) {
      lv_timer_delete(app_ctx.refresh_timer);
      app_ctx.refresh_timer = nullptr;
      UI_TRACE("Weather", "back deleted refresh timer");
    }
    UiSessionState::set_route("home");
    lv_obj_t *target_scr = (lv_obj_t *)lv_event_get_user_data(e);
    UI_TRACE("Weather", "back target valid=%d", (target_scr && lv_obj_is_valid(target_scr)) ? 1 : 0);
    if (!target_scr || !lv_obj_is_valid(target_scr)) return;
    ui_load_screen(target_scr, true);
  }
}

static void weather_app_update_ui()
{
  if (app_ctx.closing) return;
  if (!app_ctx.screen || !lv_obj_is_valid(app_ctx.screen)) return;
  if (!app_ctx.date_label || !lv_obj_is_valid(app_ctx.date_label)) return;
  if (!app_ctx.current_icon || !lv_obj_is_valid(app_ctx.current_icon)) return;
  if (!app_ctx.condition_label || !lv_obj_is_valid(app_ctx.condition_label)) return;
  if (!app_ctx.temp_label || !lv_obj_is_valid(app_ctx.temp_label)) return;
  if (!app_ctx.humidity_label || !lv_obj_is_valid(app_ctx.humidity_label)) return;
  if (!app_ctx.pressure_label || !lv_obj_is_valid(app_ctx.pressure_label)) return;
  if (!app_ctx.precip_label || !lv_obj_is_valid(app_ctx.precip_label)) return;
  if (!app_ctx.wind_label || !lv_obj_is_valid(app_ctx.wind_label)) return;

  WeatherService &weather = WeatherService::instance();

  if (!weather.has_weather()) {
    lv_label_set_text(app_ctx.condition_label, "No weather data");
    lv_label_set_text(app_ctx.temp_label, "--");
    lv_label_set_text(app_ctx.humidity_label, "--");
    lv_label_set_text(app_ctx.pressure_label, "--");
    lv_label_set_text(app_ctx.precip_label, "--");
    lv_label_set_text(app_ctx.wind_label, "--");
    if (app_ctx.wind_card_speed_label && lv_obj_is_valid(app_ctx.wind_card_speed_label))
      lv_label_set_text(app_ctx.wind_card_speed_label, "--");
    if (app_ctx.wind_card_dir_label && lv_obj_is_valid(app_ctx.wind_card_dir_label))
      lv_label_set_text(app_ctx.wind_card_dir_label, "--");
    update_wind_rose_marker(app_ctx.wind_rose_marker, 0.0f);

    for (int i = 0; i < WeatherService::FORECAST_DAYS; ++i) {
      if (app_ctx.forecast_rows[i] && lv_obj_is_valid(app_ctx.forecast_rows[i]))
        lv_obj_add_flag(app_ctx.forecast_rows[i], LV_OBJ_FLAG_HIDDEN);
    }
    for (int i = 0; i < WeatherService::FORECAST_HOURS; ++i) {
      if (app_ctx.hourly_rows[i] && lv_obj_is_valid(app_ctx.hourly_rows[i]))
        lv_obj_add_flag(app_ctx.hourly_rows[i], LV_OBJ_FLAG_HIDDEN);
    }
    return;
  }

  char date_buf[64] = {0};
  time_t now = time(nullptr);
  struct tm tm_info {};
  if (localtime_r(&now, &tm_info)) {
    strftime(date_buf, sizeof(date_buf), "%A, %d.%m.%Y", &tm_info);
  } else {
    std::snprintf(date_buf, sizeof(date_buf), "--, --.--.----");
  }
  lv_label_set_text(app_ctx.date_label, date_buf);

  weather_icon_update(app_ctx.current_icon, weather_icon_kind_from_code(weather.weather_code()), 56);
  lv_label_set_text(app_ctx.condition_label, weather_text_for_code(weather.weather_code()));

  char temp_buf[32] = {0};
  std::snprintf(temp_buf, sizeof(temp_buf), "%.1f°", weather.temperature_value());
  lv_label_set_text(app_ctx.temp_label, temp_buf);

  char humidity_buf[32] = {0};
  std::snprintf(humidity_buf, sizeof(humidity_buf), "%.0f%%", weather.humidity_value());
  lv_label_set_text(app_ctx.humidity_label, humidity_buf);

  char pressure_buf[32] = {0};
  std::snprintf(pressure_buf, sizeof(pressure_buf), "%.0f hPa", weather.pressure_value());
  lv_label_set_text(app_ctx.pressure_label, pressure_buf);

  char precip_buf[32] = {0};
  std::snprintf(precip_buf, sizeof(precip_buf), "%.1f %s", weather.precipitation_value(), weather.is_metric() ? "mm" : "in");
  lv_label_set_text(app_ctx.precip_label, precip_buf);

  char wind_buf[32] = {0};
  std::snprintf(wind_buf,
                sizeof(wind_buf),
                "%.1f %s",
                weather.wind_speed_value(),
                weather.is_metric() ? "km/h" : "mph");
  lv_label_set_text(app_ctx.wind_label, wind_buf);

  if (app_ctx.wind_card_speed_label && lv_obj_is_valid(app_ctx.wind_card_speed_label))
    lv_label_set_text(app_ctx.wind_card_speed_label, wind_buf);

  if (app_ctx.wind_card_dir_label && lv_obj_is_valid(app_ctx.wind_card_dir_label)) {
    char wind_dir_buf[32] = {0};
    std::snprintf(wind_dir_buf,
                  sizeof(wind_dir_buf),
                  "%s %.0f°",
                  wind_direction_short(weather.wind_direction_degrees()),
                  weather.wind_direction_degrees());
    lv_label_set_text(app_ctx.wind_card_dir_label, wind_dir_buf);
  }

  update_wind_rose_marker(app_ctx.wind_rose_marker, weather.wind_direction_degrees());

  const WeatherForecastDay *forecast = weather.forecast_days();
  int count = weather.forecast_count();
  for (int i = 0; i < WeatherService::FORECAST_DAYS; ++i) {
    if (!app_ctx.forecast_rows[i] || !lv_obj_is_valid(app_ctx.forecast_rows[i])) continue;
    if (!app_ctx.forecast_day_labels[i] || !lv_obj_is_valid(app_ctx.forecast_day_labels[i])) continue;
    if (!app_ctx.forecast_temp_labels[i] || !lv_obj_is_valid(app_ctx.forecast_temp_labels[i])) continue;

    if (i >= count || !forecast[i].valid) {
      lv_obj_add_flag(app_ctx.forecast_rows[i], LV_OBJ_FLAG_HIDDEN);
      continue;
    }

    lv_obj_clear_flag(app_ctx.forecast_rows[i], LV_OBJ_FLAG_HIDDEN);
    std::string date_text = weekday_label_from_iso(forecast[i].date);
    lv_label_set_text(app_ctx.forecast_day_labels[i], date_text.c_str());
    if (app_ctx.forecast_icons[i] && lv_obj_is_valid(app_ctx.forecast_icons[i])) {
      if (app_ctx.compact_list_icons)
        weather_icon_update_lightweight(app_ctx.forecast_icons[i], weather_icon_kind_from_code(forecast[i].weather_code), 40);
      else
        weather_icon_update(app_ctx.forecast_icons[i], weather_icon_kind_from_code(forecast[i].weather_code), 40);
    }

    char day_temp_buf[48] = {0};
    std::snprintf(day_temp_buf,
                  sizeof(day_temp_buf),
                  "%.0f-%.0f°",
                  forecast[i].temp_min,
                  forecast[i].temp_max);
    lv_label_set_text(app_ctx.forecast_temp_labels[i], day_temp_buf);
  }

  const WeatherForecastHour *hourly_forecast = weather.forecast_hours();
  int hourly_count = weather.hourly_forecast_count();
  for (int i = 0; i < WeatherService::FORECAST_HOURS; ++i) {
    if (!app_ctx.hourly_rows[i] || !lv_obj_is_valid(app_ctx.hourly_rows[i])) continue;
    if (!app_ctx.hourly_time_labels[i] || !lv_obj_is_valid(app_ctx.hourly_time_labels[i])) continue;
    if (!app_ctx.hourly_temp_labels[i] || !lv_obj_is_valid(app_ctx.hourly_temp_labels[i])) continue;

    if (i >= hourly_count || !hourly_forecast[i].valid) {
      lv_obj_add_flag(app_ctx.hourly_rows[i], LV_OBJ_FLAG_HIDDEN);
      continue;
    }

    lv_obj_clear_flag(app_ctx.hourly_rows[i], LV_OBJ_FLAG_HIDDEN);
    std::string hour_text = hour_label_from_iso(hourly_forecast[i].time);
    lv_label_set_text(app_ctx.hourly_time_labels[i], hour_text.c_str());
    if (app_ctx.hourly_icons[i] && lv_obj_is_valid(app_ctx.hourly_icons[i])) {
      if (app_ctx.compact_list_icons)
        weather_icon_update_lightweight(app_ctx.hourly_icons[i], weather_icon_kind_from_code(hourly_forecast[i].weather_code), 30);
      else
        weather_icon_update(app_ctx.hourly_icons[i], weather_icon_kind_from_code(hourly_forecast[i].weather_code), 30);
    }

    char hour_temp_buf[32] = {0};
    std::snprintf(hour_temp_buf, sizeof(hour_temp_buf), "%.0f°", hourly_forecast[i].temperature);
    lv_label_set_text(app_ctx.hourly_temp_labels[i], hour_temp_buf);
  }
}

static void weather_app_timer_cb(lv_timer_t *timer)
{
  (void)timer;
  if (app_ctx.closing) return;
  const bool fetching = WeatherService::instance().is_fetching();
  const unsigned long revision = WeatherService::instance().data_revision();
  if (fetching != app_ctx.was_fetching) {
    if (app_ctx.loading_line && lv_obj_is_valid(app_ctx.loading_line)) {
      if (fetching)
        lv_obj_clear_flag(app_ctx.loading_line, LV_OBJ_FLAG_HIDDEN);
      else
        lv_obj_add_flag(app_ctx.loading_line, LV_OBJ_FLAG_HIDDEN);
    }

    // Refresh the data widgets once when the background request completes.
    if (!fetching) {
      weather_app_update_ui();
      app_ctx.weather_revision = revision;
    }
  }
  if (!fetching && revision != app_ctx.weather_revision) {
    weather_app_update_ui();
    app_ctx.weather_revision = revision;
  }
  app_ctx.was_fetching = fetching;
}

static void weather_app_refresh_event(lv_event_t *e)
{
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  if (app_ctx.closing) return;

  UI_TRACE("Weather", "refresh clicked");

  if (app_ctx.condition_label && lv_obj_is_valid(app_ctx.condition_label))
    lv_label_set_text(app_ctx.condition_label, "Refreshing...");

  WeatherService &weather = WeatherService::instance();
  weather.refresh_now();
  app_ctx.was_fetching = weather.is_fetching();
  if (app_ctx.loading_line && lv_obj_is_valid(app_ctx.loading_line)) {
    if (app_ctx.was_fetching)
      lv_obj_clear_flag(app_ctx.loading_line, LV_OBJ_FLAG_HIDDEN);
    else
      lv_obj_add_flag(app_ctx.loading_line, LV_OBJ_FLAG_HIDDEN);
  }

  // Native requests complete synchronously, so there is no fetching state for
  // the timer to observe. Restore either the new or retained weather now.
  if (!app_ctx.was_fetching) {
    weather_app_update_ui();
    app_ctx.weather_revision = weather.data_revision();
  }
}

static void weather_location_step_event(lv_event_t *e)
{
  if (lv_event_get_code(e) != LV_EVENT_CLICKED || app_ctx.closing) return;
  WeatherService &weather = WeatherService::instance();
  if (weather.is_fetching()) return;

  std::vector<WeatherLocation> locations;
  int active_index = 0;
  if (!WeatherLocationStore::load(locations, active_index) || locations.size() < 2) return;
  const int delta = (int)(intptr_t)lv_event_get_user_data(e);
  active_index = (active_index + delta + (int)locations.size()) % (int)locations.size();
  if (!WeatherLocationStore::save(locations, active_index)) return;

  lv_obj_t *button = (lv_obj_t *)lv_event_get_target(e);
  lv_obj_t *selector = lv_obj_get_parent(button);
  lv_obj_t *name_label = selector ? lv_obj_get_child(selector, 1) : nullptr;
  if (name_label && lv_obj_is_valid(name_label))
    lv_label_set_text(name_label, locations[active_index].name.c_str());

  weather.reload_from_config();
  app_ctx.was_fetching = weather.is_fetching();
  if (app_ctx.loading_line && lv_obj_is_valid(app_ctx.loading_line)) {
    if (app_ctx.was_fetching) lv_obj_clear_flag(app_ctx.loading_line, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(app_ctx.loading_line, LV_OBJ_FLAG_HIDDEN);
  }
  weather_app_update_ui();
  app_ctx.weather_revision = weather.data_revision();
}

static void weather_app_screen_delete_event(lv_event_t *e)
{
  (void)e;
  UI_TRACE("Weather", "screen delete event");
  WeatherService::instance().pause();
  if (app_ctx.refresh_timer) {
    lv_timer_delete(app_ctx.refresh_timer);
    app_ctx.refresh_timer = nullptr;
  }
  app_ctx = WeatherAppCtx{};
}

}  // namespace

void weather_app_launch(void)
{
  UI_TRACE("Weather", "launch begin");
  app_ctx.closing = false;
#if defined(ARDUINO) && defined(ESP_PLATFORM) && !defined(COLOR_KIT_PRO)
  // The original ESP32 has substantially less internal RAM available to LVGL.
  // Use labels in the forecast lists instead of constructing ~100 additional
  // LVGL objects for the vector icons. Keep the detailed current-weather icon.
  app_ctx.compact_list_icons = true;
#else
  app_ctx.compact_list_icons = false;
#endif
  UI_TRACE("Weather", "compact_list_icons=%d days=%d hours=%d",
           app_ctx.compact_list_icons ? 1 : 0,
           WeatherService::FORECAST_DAYS,
           WeatherService::FORECAST_HOURS);
  lv_obj_t *home_scr = lv_screen_active();

  lv_obj_t *scr = lv_obj_create(nullptr);
  lv_obj_set_style_bg_color(scr, lv_color_hex(0x1e2030), 0);
  lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
  lv_obj_set_style_pad_all(scr, 0, 0);
  lv_obj_set_style_border_width(scr, 0, 0);
  lv_obj_add_event_cb(scr, weather_app_screen_delete_event, LV_EVENT_DELETE, nullptr);

  lv_obj_t *bar = lv_obj_create(scr);
  lv_obj_set_size(bar, lv_pct(100), 44);
  lv_obj_set_pos(bar, 0, 0);
  lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_color(bar, lv_color_hex(0x14162a), 0);
  lv_obj_set_style_border_width(bar, 0, 0);
  lv_obj_set_style_radius(bar, 0, 0);
  lv_obj_set_style_pad_all(bar, 0, 0);

  lv_obj_t *back_btn = lv_button_create(bar);
  lv_obj_set_size(back_btn, 56, 44);
  lv_obj_align(back_btn, LV_ALIGN_LEFT_MID, 4, 0);
  lv_obj_set_style_bg_opa(back_btn, LV_OPA_TRANSP, 0);
  lv_obj_set_style_shadow_width(back_btn, 0, 0);
  lv_obj_set_style_border_width(back_btn, 0, 0);
  lv_obj_add_event_cb(back_btn, nav_back_event, LV_EVENT_CLICKED, home_scr);

  lv_obj_t *back_label = lv_label_create(back_btn);
  lv_label_set_text(back_label, LV_SYMBOL_LEFT);
  lv_obj_set_style_text_color(back_label, lv_color_white(), 0);
  lv_obj_center(back_label);

  lv_obj_t *title = lv_label_create(bar);
  lv_label_set_text(title, "Weather");
  lv_obj_set_style_text_color(title, lv_color_white(), 0);
  lv_obj_align(title, LV_ALIGN_CENTER, 0, 0);

  lv_obj_t *refresh_btn = lv_button_create(bar);
  lv_obj_set_size(refresh_btn, 74, 32);
  lv_obj_align(refresh_btn, LV_ALIGN_RIGHT_MID, -34, 0);
  lv_obj_add_event_cb(refresh_btn, weather_app_refresh_event, LV_EVENT_CLICKED, nullptr);

  lv_obj_t *refresh_lbl = lv_label_create(refresh_btn);
  lv_label_set_text(refresh_lbl, "Refresh");
  lv_obj_center(refresh_lbl);

  create_time_status_label(bar, 56);
  create_wifi_status_icon(bar, -6);

  lv_obj_t *loading_line = lv_obj_create(bar);
  lv_obj_set_size(loading_line, 76, 3);
  lv_obj_set_pos(loading_line, -76, 41);
  lv_obj_clear_flag(loading_line, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_color(loading_line, lv_color_hex(0x64c8ff), 0);
  lv_obj_set_style_bg_opa(loading_line, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(loading_line, 0, 0);
  lv_obj_set_style_radius(loading_line, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_pad_all(loading_line, 0, 0);
  lv_obj_add_flag(loading_line, LV_OBJ_FLAG_HIDDEN);

  lv_anim_t loading_anim;
  lv_anim_init(&loading_anim);
  lv_anim_set_var(&loading_anim, loading_line);
  lv_anim_set_exec_cb(&loading_anim, [](void *obj, int32_t x) {
    lv_obj_set_x(static_cast<lv_obj_t *>(obj), x);
  });
  lv_anim_set_values(&loading_anim, -76, 320);
  lv_anim_set_duration(&loading_anim, 900);
  lv_anim_set_repeat_count(&loading_anim, LV_ANIM_REPEAT_INFINITE);
  lv_anim_start(&loading_anim);

  lv_obj_t *content = lv_obj_create(scr);
  lv_obj_set_pos(content, 0, 44);
  lv_obj_set_size(content, lv_pct(100), 480 - 44);
  lv_obj_set_style_bg_opa(content, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(content, 0, 0);
  lv_obj_set_style_pad_all(content, 12, 0);
  lv_obj_set_style_pad_row(content, 8, 0);
  lv_obj_set_layout(content, LV_LAYOUT_FLEX);
  lv_obj_set_flex_flow(content, LV_FLEX_FLOW_COLUMN);

  std::vector<WeatherLocation> locations;
  int active_location = 0;
  WeatherLocationStore::load(locations, active_location);
  lv_obj_t *location_selector = lv_obj_create(content);
  lv_obj_set_size(location_selector, lv_pct(100), 48);
  lv_obj_clear_flag(location_selector, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_opa(location_selector, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(location_selector, 0, 0);
  lv_obj_set_style_pad_all(location_selector, 0, 0);

  lv_obj_t *previous_location = lv_button_create(location_selector);
  lv_obj_set_size(previous_location, 56, 44);
  lv_obj_align(previous_location, LV_ALIGN_LEFT_MID, 2, 0);
  lv_obj_add_flag(previous_location, LV_OBJ_FLAG_PRESS_LOCK);
  lv_obj_add_event_cb(previous_location, weather_location_step_event, LV_EVENT_CLICKED,
                      (void *)(intptr_t)-1);
  lv_obj_t *previous_label = lv_label_create(previous_location);
  lv_label_set_text(previous_label, LV_SYMBOL_LEFT);
  lv_obj_center(previous_label);

  lv_obj_t *location_name = lv_label_create(location_selector);
  lv_obj_set_width(location_name, 160);
  lv_obj_set_style_text_align(location_name, LV_TEXT_ALIGN_CENTER, 0);
  lv_label_set_long_mode(location_name, LV_LABEL_LONG_DOT);
  lv_obj_align(location_name, LV_ALIGN_CENTER, 0, 0);
  lv_obj_set_style_text_color(location_name, lv_color_hex(0x63e6be), 0);
  lv_obj_set_style_text_font(location_name, &lv_font_montserrat_14, 0);
  lv_label_set_text(location_name,
                    locations.empty() ? "No saved location" : locations[active_location].name.c_str());

  lv_obj_t *next_location = lv_button_create(location_selector);
  lv_obj_set_size(next_location, 56, 44);
  // Keep the touch target away from the vertical scrollbar at the screen edge.
  lv_obj_align(next_location, LV_ALIGN_RIGHT_MID, -6, 0);
  lv_obj_add_flag(next_location, LV_OBJ_FLAG_PRESS_LOCK);
  lv_obj_add_event_cb(next_location, weather_location_step_event, LV_EVENT_CLICKED,
                      (void *)(intptr_t)1);
  lv_obj_t *next_label = lv_label_create(next_location);
  lv_label_set_text(next_label, LV_SYMBOL_RIGHT);
  lv_obj_center(next_label);
  if (locations.size() < 2) {
    lv_obj_add_flag(previous_location, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(next_location, LV_OBJ_FLAG_HIDDEN);
  }

  // Date and time header
  lv_obj_t *date_label = lv_label_create(content);
  lv_obj_set_style_text_color(date_label, lv_color_hex(0xb7bdd7), 0);
  lv_obj_set_style_text_font(date_label, &lv_font_montserrat_14, 0);

  lv_obj_t *condition_label = lv_label_create(content);
  lv_obj_set_style_text_color(condition_label, lv_color_white(), 0);
  lv_obj_set_style_text_font(condition_label, &lv_font_montserrat_14, 0);

  // Current temp and metrics section
  lv_obj_t *current_section = lv_obj_create(content);
  lv_obj_set_size(current_section, lv_pct(100), 88);
  lv_obj_clear_flag(current_section, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_color(current_section, lv_color_hex(0x2a2d42), 0);
  lv_obj_set_style_bg_opa(current_section, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(current_section, 0, 0);
  lv_obj_set_style_radius(current_section, 10, 0);
  lv_obj_set_style_pad_all(current_section, 12, 0);

  // Left: weather icon
  lv_obj_t *current_icon = weather_icon_create(current_section, WeatherIconKind::Cloudy, 56);
  lv_obj_align(current_icon, LV_ALIGN_LEFT_MID, 0, 0);

  // Center: large temperature
  lv_obj_t *temp_label = lv_label_create(current_section);
  lv_obj_set_style_text_color(temp_label, lv_color_white(), 0);
  lv_obj_set_style_text_font(temp_label, &lv_font_montserrat_24, 0);
  lv_obj_align(temp_label, LV_ALIGN_CENTER, -40, 0);

  // Right side metrics in a column
  lv_obj_t *humidity_label = lv_label_create(current_section);
  lv_obj_set_style_text_color(humidity_label, lv_color_hex(0xd5e2ff), 0);
  lv_obj_set_style_text_font(humidity_label, &lv_font_montserrat_14, 0);
  lv_obj_align(humidity_label, LV_ALIGN_TOP_RIGHT, -4, 0);

  lv_obj_t *pressure_label = lv_label_create(current_section);
  lv_obj_set_style_text_color(pressure_label, lv_color_hex(0xd5e2ff), 0);
  lv_obj_set_style_text_font(pressure_label, &lv_font_montserrat_14, 0);
  lv_obj_align(pressure_label, LV_ALIGN_RIGHT_MID, -4, 0);

  lv_obj_t *wind_label = lv_label_create(current_section);
  lv_obj_set_style_text_color(wind_label, lv_color_hex(0xd5e2ff), 0);
  lv_obj_set_style_text_font(wind_label, &lv_font_montserrat_14, 0);
  lv_obj_align(wind_label, LV_ALIGN_BOTTOM_RIGHT, -4, 0);

  lv_obj_t *precip_label = lv_label_create(current_section);
  lv_obj_set_style_text_color(precip_label, lv_color_hex(0xd5e2ff), 0);
  lv_obj_set_style_text_font(precip_label, &lv_font_montserrat_14, 0);
  lv_obj_align(precip_label, LV_ALIGN_BOTTOM_LEFT, 70, 0);

  // Hourly forecast card with horizontal scrolling
  lv_obj_t *hourly_card = lv_obj_create(content);
  lv_obj_set_size(hourly_card, lv_pct(100), 102);
  lv_obj_set_style_bg_color(hourly_card, lv_color_hex(0x2a2d42), 0);
  lv_obj_set_style_bg_opa(hourly_card, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(hourly_card, 0, 0);
  lv_obj_set_style_radius(hourly_card, 10, 0);
  lv_obj_set_style_pad_all(hourly_card, 8, 0);
  lv_obj_set_style_pad_row(hourly_card, 0, 0);
  lv_obj_set_layout(hourly_card, LV_LAYOUT_FLEX);
  lv_obj_set_flex_flow(hourly_card, LV_FLEX_FLOW_COLUMN);
  lv_obj_clear_flag(hourly_card, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_scrollbar_mode(hourly_card, LV_SCROLLBAR_MODE_OFF);

  lv_obj_t *hourly_strip = lv_obj_create(hourly_card);
  lv_obj_set_width(hourly_strip, lv_pct(100));
  lv_obj_set_height(hourly_strip, 86);
  lv_obj_set_style_bg_opa(hourly_strip, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(hourly_strip, 0, 0);
  lv_obj_set_style_pad_all(hourly_strip, 0, 0);
  lv_obj_set_style_pad_bottom(hourly_strip, 8, 0);
  lv_obj_set_style_pad_column(hourly_strip, 8, 0);
  lv_obj_set_layout(hourly_strip, LV_LAYOUT_FLEX);
  lv_obj_set_flex_flow(hourly_strip, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(hourly_strip, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_scroll_dir(hourly_strip, LV_DIR_HOR);
  lv_obj_set_scrollbar_mode(hourly_strip, LV_SCROLLBAR_MODE_OFF);
  lv_obj_clear_flag(hourly_strip, LV_OBJ_FLAG_SCROLL_CHAIN_VER);

  for (int i = 0; i < WeatherService::FORECAST_HOURS; ++i) {
    lv_obj_t *col = lv_obj_create(hourly_strip);
    if (!col) {
      UI_TRACE("Weather", "hourly col alloc failed i=%d", i);
      break;
    }
    lv_obj_set_size(col, 56, 82);
    lv_obj_clear_flag(col, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(col, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(col, 0, 0);
    lv_obj_set_style_radius(col, 0, 0);
    lv_obj_set_style_pad_all(col, 2, 0);
    lv_obj_set_layout(col, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(col, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t *hour_label = lv_label_create(col);
    if (!hour_label) {
      UI_TRACE("Weather", "hour label alloc failed i=%d", i);
      lv_obj_delete(col);
      break;
    }
    lv_obj_set_style_text_color(hour_label, lv_color_white(), 0);
    lv_obj_set_style_text_font(hour_label, &lv_font_montserrat_14, 0);

    lv_obj_t *icon = nullptr;
    if (app_ctx.compact_list_icons) {
      icon = weather_icon_create_lightweight(col, WeatherIconKind::Cloudy, 30);
    } else {
      icon = weather_icon_create(col, WeatherIconKind::Cloudy, 30);
    }
    if (!icon)
      UI_TRACE("Weather", "hour icon alloc failed i=%d", i);

    lv_obj_t *temp = lv_label_create(col);
    if (!temp) {
      UI_TRACE("Weather", "hour temp alloc failed i=%d", i);
      lv_obj_delete(col);
      break;
    }
    lv_obj_set_style_text_color(temp, lv_color_hex(0xd5e2ff), 0);
    lv_obj_set_style_text_font(temp, &lv_font_montserrat_14, 0);

    app_ctx.hourly_rows[i] = col;
    app_ctx.hourly_time_labels[i] = hour_label;
    app_ctx.hourly_icons[i] = icon;
    app_ctx.hourly_temp_labels[i] = temp;
  }

  // Forecast grid: show all configured day slots
  lv_obj_t *forecast_card = lv_obj_create(content);
  lv_obj_set_size(forecast_card, lv_pct(100), 110);
  lv_obj_clear_flag(forecast_card, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_color(forecast_card, lv_color_hex(0x2a2d42), 0);
  lv_obj_set_style_bg_opa(forecast_card, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(forecast_card, 0, 0);
  lv_obj_set_style_radius(forecast_card, 10, 0);
  lv_obj_set_style_pad_all(forecast_card, 8, 0);
  lv_obj_set_layout(forecast_card, LV_LAYOUT_FLEX);
  lv_obj_set_flex_flow(forecast_card, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(forecast_card, LV_FLEX_ALIGN_SPACE_AROUND, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START);

  // Create forecast columns for all configured day slots
  for (int i = 0; i < WeatherService::FORECAST_DAYS; ++i) {
    lv_obj_t *col = lv_obj_create(forecast_card);
    if (!col) {
      UI_TRACE("Weather", "forecast col alloc failed i=%d", i);
      break;
    }
    lv_obj_set_size(col, 70, 90);
    lv_obj_clear_flag(col, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(col, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(col, 0, 0);
    lv_obj_set_style_pad_all(col, 4, 0);
    lv_obj_set_layout(col, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(col, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t *day_label = lv_label_create(col);
    if (!day_label) {
      UI_TRACE("Weather", "forecast day alloc failed i=%d", i);
      lv_obj_delete(col);
      break;
    }
    lv_obj_set_style_text_color(day_label, lv_color_white(), 0);
    lv_obj_set_style_text_font(day_label, &lv_font_montserrat_14, 0);

    lv_obj_t *icon = nullptr;
    if (app_ctx.compact_list_icons) {
      icon = weather_icon_create_lightweight(col, WeatherIconKind::Cloudy, 40);
    } else {
      icon = weather_icon_create(col, WeatherIconKind::Cloudy, 40);
    }
    if (!icon)
      UI_TRACE("Weather", "forecast icon alloc failed i=%d", i);

    lv_obj_t *temp = lv_label_create(col);
    if (!temp) {
      UI_TRACE("Weather", "forecast temp alloc failed i=%d", i);
      lv_obj_delete(col);
      break;
    }
    lv_obj_set_style_text_color(temp, lv_color_hex(0xd5e2ff), 0);
    lv_obj_set_style_text_font(temp, &lv_font_montserrat_14, 0);

    app_ctx.forecast_rows[i] = col;
    app_ctx.forecast_day_labels[i] = day_label;
    app_ctx.forecast_icons[i] = icon;
    app_ctx.forecast_temp_labels[i] = temp;
  }

  // Wind card with wind-rose style direction marker
  lv_obj_t *wind_card = lv_obj_create(content);
  lv_obj_set_size(wind_card, lv_pct(100), 96);
  lv_obj_clear_flag(wind_card, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_color(wind_card, lv_color_hex(0x2a2d42), 0);
  lv_obj_set_style_bg_opa(wind_card, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(wind_card, 0, 0);
  lv_obj_set_style_radius(wind_card, 10, 0);
  lv_obj_set_style_pad_all(wind_card, 10, 0);

  lv_obj_t *wind_title = lv_label_create(wind_card);
  lv_label_set_text(wind_title, "Wind");
  lv_obj_set_style_text_color(wind_title, lv_color_white(), 0);
  lv_obj_set_style_text_font(wind_title, &lv_font_montserrat_14, 0);
  lv_obj_align(wind_title, LV_ALIGN_TOP_LEFT, 0, 0);

  lv_obj_t *wind_card_speed_label = lv_label_create(wind_card);
  lv_obj_set_style_text_color(wind_card_speed_label, lv_color_hex(0xd5e2ff), 0);
  lv_obj_set_style_text_font(wind_card_speed_label, &lv_font_montserrat_24, 0);
  lv_obj_align(wind_card_speed_label, LV_ALIGN_LEFT_MID, 0, 8);

  lv_obj_t *wind_card_dir_label = lv_label_create(wind_card);
  lv_obj_set_style_text_color(wind_card_dir_label, lv_color_hex(0xb7bdd7), 0);
  lv_obj_set_style_text_font(wind_card_dir_label, &lv_font_montserrat_14, 0);
  lv_obj_align(wind_card_dir_label, LV_ALIGN_BOTTOM_LEFT, 0, 0);

  lv_obj_t *wind_rose = lv_obj_create(wind_card);
  lv_obj_set_size(wind_rose, 72, 72);
  lv_obj_set_style_bg_opa(wind_rose, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_color(wind_rose, lv_color_hex(0x7d88a5), 0);
  lv_obj_set_style_border_width(wind_rose, 1, 0);
  lv_obj_set_style_radius(wind_rose, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_pad_all(wind_rose, 0, 0);
  lv_obj_align(wind_rose, LV_ALIGN_RIGHT_MID, 0, 0);

  lv_obj_t *n = lv_label_create(wind_rose);
  lv_label_set_text(n, "N");
  lv_obj_set_style_text_color(n, lv_color_white(), 0);
  lv_obj_set_style_text_font(n, &lv_font_montserrat_14, 0);
  lv_obj_align(n, LV_ALIGN_TOP_MID, 0, 0);

  lv_obj_t *e = lv_label_create(wind_rose);
  lv_label_set_text(e, "E");
  lv_obj_set_style_text_color(e, lv_color_hex(0xb7bdd7), 0);
  lv_obj_set_style_text_font(e, &lv_font_montserrat_14, 0);
  lv_obj_align(e, LV_ALIGN_RIGHT_MID, -2, 0);

  lv_obj_t *s = lv_label_create(wind_rose);
  lv_label_set_text(s, "S");
  lv_obj_set_style_text_color(s, lv_color_hex(0xb7bdd7), 0);
  lv_obj_set_style_text_font(s, &lv_font_montserrat_14, 0);
  lv_obj_align(s, LV_ALIGN_BOTTOM_MID, 0, -1);

  lv_obj_t *w = lv_label_create(wind_rose);
  lv_label_set_text(w, "W");
  lv_obj_set_style_text_color(w, lv_color_hex(0xb7bdd7), 0);
  lv_obj_set_style_text_font(w, &lv_font_montserrat_14, 0);
  lv_obj_align(w, LV_ALIGN_LEFT_MID, 2, 0);

  lv_obj_t *wind_rose_marker = lv_obj_create(wind_rose);
  lv_obj_set_size(wind_rose_marker, 8, 8);
  lv_obj_set_style_bg_color(wind_rose_marker, lv_color_hex(0x5cb2ff), 0);
  lv_obj_set_style_bg_opa(wind_rose_marker, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(wind_rose_marker, 0, 0);
  lv_obj_set_style_radius(wind_rose_marker, LV_RADIUS_CIRCLE, 0);
  update_wind_rose_marker(wind_rose_marker, 0.0f);

  app_ctx.screen = scr;
  app_ctx.date_label = date_label;
  app_ctx.current_icon = current_icon;
  app_ctx.condition_label = condition_label;
  app_ctx.temp_label = temp_label;
  app_ctx.humidity_label = humidity_label;
  app_ctx.pressure_label = pressure_label;
  app_ctx.precip_label = precip_label;
  app_ctx.wind_label = wind_label;
  app_ctx.wind_card_speed_label = wind_card_speed_label;
  app_ctx.wind_card_dir_label = wind_card_dir_label;
  app_ctx.wind_rose_marker = wind_rose_marker;
  app_ctx.refresh_btn = refresh_btn;
  app_ctx.loading_line = loading_line;
  app_ctx.refresh_timer = lv_timer_create(weather_app_timer_cb, 100, nullptr);

  WeatherService::instance().resume();
  app_ctx.was_fetching = WeatherService::instance().is_fetching();
  app_ctx.weather_revision = WeatherService::instance().data_revision();
  weather_app_update_ui();
  ui_load_screen(scr, false);
  UI_TRACE("Weather", "launch loaded screen");
  
  // Set route after screen is loaded
  UiSessionState::set_route("weather");
}
