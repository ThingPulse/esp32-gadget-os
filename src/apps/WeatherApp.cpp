#include "apps/WeatherApp.h"

#include "AppRegistry.h"
#include "services/WeatherService.h"
#include "services/WeatherLocationStore.h"
#include "ui/UiNav.h"
#include "ui/StatusIcons.h"
#include "ui/UiSessionState.h"
#include "ui/UiTrace.h"
#include "ui/WeatherIconSet.h"
#include "ui/ClassicWeatherView.h"
#include "services/TimeService.h"
#include "services/ConfigStoreService.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <string>
#include <vector>

static lv_obj_t *weather_launcher_icon_create(lv_obj_t *parent, lv_coord_t size)
{
  lv_obj_t *icon = weather_icon_create_lightweight(parent, WeatherIconKind::Clear, size);
  if (icon) lv_obj_set_style_text_color(icon, lv_color_white(), 0);
  return icon;
}

APP_REGISTER_CUSTOM_ICON_COLOR_RANK("Weather", weather_launcher_icon_create,
                                    weather_app_launch, 0x339af0, 10);

namespace {

struct WeatherAppCtx {
  bool classic = false;
  ClassicWeatherView classic_view;
  lv_obj_t *screen = nullptr;
  lv_obj_t *pager = nullptr;
  lv_obj_t *content = nullptr;
  lv_obj_t *location_name = nullptr;
  lv_obj_t *page_indicator = nullptr;
  lv_obj_t *previous_location = nullptr;
  lv_obj_t *next_location = nullptr;
  std::vector<WeatherLocation> locations;
  std::vector<lv_obj_t *> pages;
  std::vector<lv_obj_t *> previews;
  int active_location = 0;
  int pending_location = -1;

  lv_obj_t *date_label = nullptr;
  lv_obj_t *local_clock = nullptr;
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
  lv_obj_t *sun_times_label = nullptr;
  lv_obj_t *moon_phase_label = nullptr;
  lv_obj_t *moon_shadow = nullptr;
  lv_obj_t *pull_indicator = nullptr;
  lv_obj_t *loading_line = nullptr;
  lv_timer_t *refresh_timer = nullptr;
  unsigned long weather_revision = 0;
  bool was_fetching = false;
  bool pull_armed = false;
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

static void update_moon_visual(lv_obj_t *shadow, float phase)
{
  if (!shadow || !lv_obj_is_valid(shadow)) return;
  phase = std::fmod(phase, 1.0f);
  if (phase < 0.0f) phase += 1.0f;
  const int x = phase < 0.5f
                    ? -(int)std::lround(84.0f * phase)
                    : (int)std::lround(84.0f * (1.0f - phase));
  lv_obj_set_pos(shadow, x, 0);
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

static void weather_update_local_clock()
{
  if (app_ctx.classic) return;
  tm local{};
  const bool valid = TimeService::instance().is_synchronized() &&
                     WeatherService::instance().local_time(time(nullptr), local);
  char text[64] = "--:--";
  if (valid) strftime(text, sizeof(text), TimeService::instance().use_24h() ? "%H:%M" : "%I:%M %p", &local);
  if (app_ctx.local_clock && std::strcmp(lv_label_get_text(app_ctx.local_clock), text) != 0)
    lv_label_set_text(app_ctx.local_clock, text);
  if (app_ctx.date_label) {
    if (valid) strftime(text, sizeof(text), "%A, %d.%m.%Y", &local);
    else std::snprintf(text, sizeof(text), "--, --.--.----");
    if (std::strcmp(lv_label_get_text(app_ctx.date_label), text) != 0)
      lv_label_set_text(app_ctx.date_label, text);
  }
}

static void weather_app_update_ui()
{
  if (app_ctx.closing) return;
  if (!app_ctx.screen || !lv_obj_is_valid(app_ctx.screen)) return;
  if (app_ctx.classic) { classic_weather_update(app_ctx.classic_view); return; }
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
    if (app_ctx.sun_times_label && lv_obj_is_valid(app_ctx.sun_times_label))
      lv_label_set_text(app_ctx.sun_times_label, "Rise --:--\nSet  --:--");
    if (app_ctx.moon_phase_label && lv_obj_is_valid(app_ctx.moon_phase_label))
      lv_label_set_text(app_ctx.moon_phase_label, "Moon data\nunavailable");
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

  weather_update_local_clock();

  weather_icon_update(app_ctx.current_icon, weather_icon_kind_from_code(weather.weather_code()), 56);
  lv_label_set_text(app_ctx.condition_label, weather_text_for_code(weather.weather_code()));

  char temp_buf[32] = {0};
  std::snprintf(temp_buf, sizeof(temp_buf), "%.1f°", weather.temperature_value());
  lv_label_set_text(app_ctx.temp_label, temp_buf);

  char humidity_buf[32] = {0};
  std::snprintf(humidity_buf, sizeof(humidity_buf), "%.0f%%", weather.humidity_value());
  lv_label_set_text(app_ctx.humidity_label, humidity_buf);

  char pressure_buf[32] = {0};
  std::snprintf(pressure_buf, sizeof(pressure_buf), weather.is_metric() ? "%.0f hPa" : "%.2f inHg", weather.pressure_value());
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

  if (app_ctx.sun_times_label && lv_obj_is_valid(app_ctx.sun_times_label)) {
    const std::string sunrise = hour_label_from_iso(weather.sunrise_time());
    const std::string sunset = hour_label_from_iso(weather.sunset_time());
    char sun_text[48] = {};
    std::snprintf(sun_text, sizeof(sun_text), "Rise %s\nSet  %s",
                  sunrise.c_str(), sunset.c_str());
    lv_label_set_text(app_ctx.sun_times_label, sun_text);
  }
  if (app_ctx.moon_phase_label && lv_obj_is_valid(app_ctx.moon_phase_label)) {
    char moon_text[64] = {};
    std::snprintf(moon_text, sizeof(moon_text), "%s\n%.0f%% lit",
                  weather.moon_phase_name(), weather.moon_illumination_percent());
    lv_label_set_text(app_ctx.moon_phase_label, moon_text);
  }
  update_moon_visual(app_ctx.moon_shadow, weather.moon_phase_fraction());

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

static void weather_apply_selected_location();

static void weather_app_timer_cb(lv_timer_t *timer)
{
  (void)timer;
  if (app_ctx.closing) return;
  if (app_ctx.classic) classic_weather_tick(app_ctx.classic_view);
  else weather_update_local_clock();
  weather_apply_selected_location();
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

static void weather_app_start_refresh(const char *source)
{
  if (app_ctx.closing) return;
  WeatherService &weather = WeatherService::instance();
  if (weather.is_fetching()) return;

  UI_TRACE("Weather", "refresh requested source=%s", source ? source : "unknown");

  if (app_ctx.condition_label && lv_obj_is_valid(app_ctx.condition_label))
    lv_label_set_text(app_ctx.condition_label, "Refreshing...");

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

static void weather_pull_to_refresh_event(lv_event_t *e)
{
  static constexpr int32_t PULL_SHOW_PX = 8;
  static constexpr int32_t PULL_TRIGGER_PX = 56;

  lv_obj_t *content = (lv_obj_t *)lv_event_get_target(e);
  if (!content || app_ctx.closing) return;

  const lv_event_code_t code = lv_event_get_code(e);
  if (code == LV_EVENT_SCROLL) {
    const int32_t scroll_y = lv_obj_get_scroll_y(content);
    const int32_t pull = scroll_y < 0 ? -scroll_y : 0;
    if (WeatherService::instance().is_fetching()) return;

    if (pull >= PULL_TRIGGER_PX) app_ctx.pull_armed = true;
    if (app_ctx.pull_indicator && lv_obj_is_valid(app_ctx.pull_indicator)) {
      if (pull >= PULL_SHOW_PX || app_ctx.pull_armed) {
        lv_label_set_text(app_ctx.pull_indicator,
                          app_ctx.pull_armed ? "Release to refresh" : "Pull to refresh");
        lv_obj_clear_flag(app_ctx.pull_indicator, LV_OBJ_FLAG_HIDDEN);
      } else {
        lv_obj_add_flag(app_ctx.pull_indicator, LV_OBJ_FLAG_HIDDEN);
      }
    }
  } else if (code == LV_EVENT_SCROLL_END) {
    const bool refresh = app_ctx.pull_armed;
    app_ctx.pull_armed = false;
    if (app_ctx.pull_indicator && lv_obj_is_valid(app_ctx.pull_indicator))
      lv_obj_add_flag(app_ctx.pull_indicator, LV_OBJ_FLAG_HIDDEN);
    if (refresh) weather_app_start_refresh("pull");
  }
}

static void weather_apply_selected_location()
{
  WeatherService &weather = WeatherService::instance();
  const int index = app_ctx.pending_location;
  if (app_ctx.closing || index < 0 || weather.is_fetching() || lv_obj_is_scrolling(app_ctx.pager)) return;
  app_ctx.pending_location = -1;
  if (index == app_ctx.active_location) return;
  if (!WeatherLocationStore::save(app_ctx.locations, index)) {
    lv_obj_scroll_to_x(app_ctx.pager, app_ctx.active_location * lv_obj_get_width(app_ctx.pager), LV_ANIM_ON);
    return;
  }

  // Keep only one full forecast widget tree on memory-constrained boards.
  // Adjacent pages remain lightweight previews while the pager is dragged.
  const int previous = app_ctx.active_location;
  lv_label_set_text_fmt(app_ctx.previews[previous], "%s\n%s\n%s",
                        app_ctx.locations[previous].name.c_str(),
                        lv_label_get_text(app_ctx.temp_label),
                        lv_label_get_text(app_ctx.condition_label));
  lv_obj_clear_flag(app_ctx.previews[previous], LV_OBJ_FLAG_HIDDEN);
  lv_obj_set_parent(app_ctx.content, app_ctx.pages[index]);
  lv_obj_add_flag(app_ctx.previews[index], LV_OBJ_FLAG_HIDDEN);
  lv_obj_scroll_to_y(app_ctx.content, 0, LV_ANIM_OFF);
  app_ctx.pull_armed = false;
  lv_obj_add_flag(app_ctx.pull_indicator, LV_OBJ_FLAG_HIDDEN);
  app_ctx.active_location = index;
  lv_label_set_text(app_ctx.location_name, app_ctx.locations[index].name.c_str());
  weather.reload_from_config();
  app_ctx.was_fetching = weather.is_fetching();
  if (app_ctx.was_fetching) lv_obj_clear_flag(app_ctx.loading_line, LV_OBJ_FLAG_HIDDEN);
  else lv_obj_add_flag(app_ctx.loading_line, LV_OBJ_FLAG_HIDDEN);
  weather_app_update_ui();
  app_ctx.weather_revision = weather.data_revision();
  UI_TRACE("Weather", "carousel location=%d name=%s", index, app_ctx.locations[index].name.c_str());
}

static void weather_update_navigation(int index)
{
  if (app_ctx.locations.empty()) return;
  lv_label_set_text(app_ctx.location_name, app_ctx.locations[index].name.c_str());
  if (app_ctx.page_indicator)
    lv_label_set_text_fmt(app_ctx.page_indicator, "%d / %d", index + 1, (int)app_ctx.locations.size());
  if (app_ctx.previous_location) {
    if (index == 0) lv_obj_add_state(app_ctx.previous_location, LV_STATE_DISABLED);
    else lv_obj_remove_state(app_ctx.previous_location, LV_STATE_DISABLED);
  }
  if (app_ctx.next_location) {
    if (index + 1 == (int)app_ctx.locations.size()) lv_obj_add_state(app_ctx.next_location, LV_STATE_DISABLED);
    else lv_obj_remove_state(app_ctx.next_location, LV_STATE_DISABLED);
  }
}

static void weather_pager_event(lv_event_t *e)
{
  if (app_ctx.closing || lv_event_get_target(e) != app_ctx.pager) return;
  const int width = lv_obj_get_width(app_ctx.pager);
  if (width <= 0 || app_ctx.locations.empty()) return;
  const int index = LV_CLAMP(0, (lv_obj_get_scroll_x(app_ctx.pager) + width / 2) / width,
                            (int)app_ctx.locations.size() - 1);
  weather_update_navigation(index);
  if (lv_event_get_code(e) == LV_EVENT_SCROLL_END) {
    app_ctx.pending_location = index;
    // The timer applies the selection after snapping and any active request.
    // A newer swipe replaces this selection instead of changing worker inputs.
  }
}

static void weather_location_step_event(lv_event_t *e)
{
  if (lv_event_get_code(e) != LV_EVENT_CLICKED || app_ctx.closing || app_ctx.locations.size() < 2) return;
  const int delta = (int)(intptr_t)lv_event_get_user_data(e);
  const int width = lv_obj_get_width(app_ctx.pager);
  if (width <= 0) return;
  const int current = (lv_obj_get_scroll_x(app_ctx.pager) + width / 2) / width;
  const int index = LV_CLAMP(0, current + delta, (int)app_ctx.locations.size() - 1);
  lv_obj_scroll_to_x(app_ctx.pager, index * lv_obj_get_width(app_ctx.pager), LV_ANIM_ON);
}

static void weather_show_classic_intro()
{
  if (!app_ctx.classic) return;
  auto &config = ConfigStoreService::instance();
  if (config.load("weather") && config.get_bool("classic_intro_seen", false)) return;

  // Keep the modal owned by this screen so it cannot outlive the Weather app.
  lv_obj_t *overlay = lv_obj_create(app_ctx.screen);
  lv_obj_set_size(overlay, lv_pct(100), lv_pct(100));
  lv_obj_set_style_pad_all(overlay, 0, 0);
  lv_obj_set_style_border_width(overlay, 0, 0);
  lv_obj_set_style_radius(overlay, 0, 0);
  lv_obj_set_style_bg_color(overlay, lv_color_black(), 0);
  lv_obj_set_style_bg_opa(overlay, LV_OPA_60, 0);
  lv_obj_clear_flag(overlay, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t *dialog = lv_obj_create(overlay);
  lv_obj_set_size(dialog, 280, 230);
  lv_obj_center(dialog);
  lv_obj_set_style_bg_color(dialog, lv_color_hex(0x2a2d42), 0);
  lv_obj_set_style_border_width(dialog, 0, 0);
  lv_obj_set_style_pad_all(dialog, 16, 0);
  lv_obj_clear_flag(dialog, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t *title = lv_label_create(dialog);
  lv_label_set_text(title, "Classic weather view");
  lv_obj_set_style_text_color(title, lv_color_hex(0x63e6be), 0);
  lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 0);
  lv_obj_t *body = lv_label_create(dialog);
  lv_obj_set_width(body, lv_pct(100));
  lv_label_set_long_mode(body, LV_LABEL_LONG_WRAP);
  lv_label_set_text(body, "We have a new layout for you.\n\nPrefer the old one? Change it in Settings > Weather");
  lv_obj_set_style_text_color(body, lv_color_white(), 0);
  lv_obj_set_pos(body, 0, 30);

  lv_obj_t *button = lv_button_create(dialog);
  lv_obj_set_size(button, 120, 40);
  lv_obj_align(button, LV_ALIGN_BOTTOM_MID, 0, 0);
  lv_obj_t *label = lv_label_create(button);
  lv_label_set_text(label, "Got it");
  lv_obj_center(label);
  lv_obj_add_event_cb(button, [](lv_event_t *event) {
    auto &config = ConfigStoreService::instance();
    // Persist on acknowledgment, so an interrupted first launch shows it again.
    if (config.load("weather")) {
      config.set("classic_intro_seen", true);
      config.save();
    }
    lv_obj_delete_async((lv_obj_t *)lv_event_get_user_data(event));
  }, LV_EVENT_CLICKED, overlay);
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
  auto &config = ConfigStoreService::instance();
  app_ctx.classic = !config.load("weather") || config.get("layout", "classic") == "classic";
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
  app_ctx.location_name = title;
  lv_label_set_text(title, "Weather");
  lv_obj_set_width(title, lv_display_get_horizontal_resolution(nullptr) - 144);
  lv_label_set_long_mode(title, LV_LABEL_LONG_DOT);
  lv_obj_set_height(title, lv_font_get_line_height(lv_obj_get_style_text_font(title, LV_PART_MAIN)));
  lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_text_color(title, lv_color_hex(0x63e6be), 0);
  lv_obj_align(title, LV_ALIGN_LEFT_MID, 110, 0);

  if (app_ctx.classic) {
    lv_obj_set_width(title, lv_display_get_horizontal_resolution(nullptr) - 112);
    lv_obj_align(title, LV_ALIGN_LEFT_MID, 56, 0);
  } else {
    app_ctx.local_clock = lv_label_create(bar);
    lv_obj_align(app_ctx.local_clock, LV_ALIGN_LEFT_MID, 56, 0);
    lv_obj_set_style_text_color(app_ctx.local_clock, lv_color_white(), 0);
    weather_update_local_clock();
  }
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

  std::vector<WeatherLocation> locations;
  int active_location = 0;
  WeatherLocationStore::load(locations, active_location);
  app_ctx.locations = locations;
  app_ctx.active_location = active_location;

  // Match Home's horizontally snapping, one-page-at-a-time carousel.
  lv_obj_t *pager = lv_obj_create(scr);
  app_ctx.pager = pager;
  lv_obj_set_pos(pager, 0, 44);
  lv_obj_set_size(pager, lv_pct(100), lv_display_get_vertical_resolution(nullptr) - 44 -
                  (locations.size() > 1 ? 40 : 0));
  lv_obj_set_style_bg_opa(pager, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(pager, 0, 0);
  lv_obj_set_style_radius(pager, 0, 0);
  lv_obj_set_style_pad_all(pager, 0, 0);
  lv_obj_set_style_pad_column(pager, 0, 0);
  lv_obj_set_scroll_dir(pager, locations.size() > 1 ? LV_DIR_HOR : LV_DIR_NONE);
  lv_obj_set_scroll_snap_x(pager, LV_SCROLL_SNAP_CENTER);
  lv_obj_add_flag(pager, LV_OBJ_FLAG_SCROLL_ONE);
  lv_obj_set_scrollbar_mode(pager, LV_SCROLLBAR_MODE_OFF);
  lv_obj_set_layout(pager, LV_LAYOUT_FLEX);
  lv_obj_set_flex_flow(pager, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(pager, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
  for (size_t i = 0; i < std::max(size_t(1), locations.size()); ++i) {
    lv_obj_t *page = lv_obj_create(pager);
    lv_obj_set_size(page, lv_pct(100), lv_pct(100));
    lv_obj_clear_flag(page, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(page, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(page, 0, 0);
    lv_obj_set_style_pad_all(page, 0, 0);
    lv_obj_set_style_radius(page, 0, 0);
    app_ctx.pages.push_back(page);
    lv_obj_t *preview = lv_label_create(page);
    lv_obj_set_width(preview, lv_pct(90));
    lv_obj_set_style_text_align(preview, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(preview, lv_color_hex(0x63e6be), 0);
    lv_label_set_text_fmt(preview, "%s\nForecast", locations.empty() ? "No saved location" : locations[i].name.c_str());
    lv_obj_align(preview, LV_ALIGN_TOP_MID, 0, 24);
    app_ctx.previews.push_back(preview);
  }
  lv_obj_add_flag(app_ctx.previews[active_location], LV_OBJ_FLAG_HIDDEN);
  if (locations.size() > 1) {
    lv_obj_t *footer = lv_obj_create(scr);
    lv_obj_set_size(footer, lv_pct(100), 40);
    lv_obj_align(footer, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_clear_flag(footer, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(footer, lv_color_hex(0x1e2030), 0);
    lv_obj_set_style_bg_opa(footer, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(footer, 0, 0);
    lv_obj_set_style_radius(footer, 0, 0);
    lv_obj_set_style_pad_all(footer, 0, 0);
    app_ctx.page_indicator = lv_label_create(footer);
    lv_label_set_text_fmt(app_ctx.page_indicator, "%d / %d", active_location + 1, (int)locations.size());
    lv_obj_set_width(app_ctx.page_indicator, 64);
    lv_obj_set_style_text_align(app_ctx.page_indicator, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_bg_color(app_ctx.page_indicator, lv_color_hex(0x1e2030), 0);
    lv_obj_set_style_bg_opa(app_ctx.page_indicator, LV_OPA_COVER, 0);
    lv_obj_set_style_text_color(app_ctx.page_indicator, lv_color_hex(0x868e96), 0);
    lv_obj_center(app_ctx.page_indicator);
    auto navigation_button = [&](int direction) {
      lv_obj_t *button = lv_button_create(footer);
      lv_obj_set_size(button, 56, 40);
      lv_obj_align(button, LV_ALIGN_CENTER, direction * 64, 0);
      lv_obj_set_style_bg_opa(button, LV_OPA_TRANSP, LV_STATE_DEFAULT);
      lv_obj_set_style_bg_opa(button, LV_OPA_TRANSP, LV_STATE_DISABLED);
      lv_obj_set_style_shadow_width(button, 0, 0);
      lv_obj_set_style_border_width(button, 0, 0);
      lv_obj_set_style_text_color(button, lv_color_hex(0xd7dde6), LV_STATE_DEFAULT);
      lv_obj_set_style_text_color(button, lv_color_hex(0x50586a), LV_STATE_DISABLED);
      lv_obj_add_event_cb(button, weather_location_step_event, LV_EVENT_CLICKED,
                          (void *)(intptr_t)direction);
      lv_obj_t *arrow = lv_label_create(button);
      lv_label_set_text(arrow, direction < 0 ? LV_SYMBOL_LEFT : LV_SYMBOL_RIGHT);
      lv_obj_center(arrow);
      return button;
    };
    app_ctx.previous_location = navigation_button(-1);
    app_ctx.next_location = navigation_button(1);
  }

  lv_obj_t *content = lv_obj_create(app_ctx.pages[active_location]);
  app_ctx.content = content;
  lv_obj_set_pos(content, 0, 0);
  lv_obj_set_size(content, lv_pct(100), lv_pct(100));
  lv_obj_set_scroll_dir(content, LV_DIR_VER);
  lv_obj_set_style_bg_opa(content, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(content, 0, 0);
  lv_obj_set_style_pad_all(content, 12, 0);
  lv_obj_set_style_pad_row(content, 8, 0);
  lv_obj_set_layout(content, LV_LAYOUT_FLEX);
  lv_obj_set_flex_flow(content, LV_FLEX_FLOW_COLUMN);
  lv_obj_add_flag(content, LV_OBJ_FLAG_SCROLL_ELASTIC);

  if (app_ctx.classic) {
    lv_obj_set_layout(content, LV_LAYOUT_NONE);
    lv_obj_set_style_pad_all(content, 0, 0);
    classic_weather_create(app_ctx.classic_view, content);
    app_ctx.temp_label = app_ctx.classic_view.temperature;
    app_ctx.condition_label = app_ctx.classic_view.condition;
  } else {
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
  // Horizontal drags here belong to the hourly forecast, even at its edges.
  lv_obj_clear_flag(hourly_strip, LV_OBJ_FLAG_SCROLL_CHAIN_VER);
  lv_obj_clear_flag(hourly_strip, LV_OBJ_FLAG_SCROLL_CHAIN_HOR);

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

  // Astronomy card. Sunrise/sunset come from the existing Open-Meteo
  // response; moon phase and illumination are calculated locally.
  lv_obj_t *astronomy_card = lv_obj_create(content);
  lv_obj_set_size(astronomy_card, lv_pct(100), 118);
  lv_obj_clear_flag(astronomy_card, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_color(astronomy_card, lv_color_hex(0x2a2d42), 0);
  lv_obj_set_style_bg_opa(astronomy_card, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(astronomy_card, 0, 0);
  lv_obj_set_style_radius(astronomy_card, 10, 0);
  lv_obj_set_style_pad_all(astronomy_card, 10, 0);

  lv_obj_t *astronomy_title = lv_label_create(astronomy_card);
  lv_label_set_text(astronomy_title, "Sun & Moon");
  lv_obj_set_style_text_color(astronomy_title, lv_color_white(), 0);
  lv_obj_align(astronomy_title, LV_ALIGN_TOP_LEFT, 0, 0);

  lv_obj_t *divider = lv_obj_create(astronomy_card);
  lv_obj_set_size(divider, 1, 76);
  lv_obj_align(divider, LV_ALIGN_BOTTOM_MID, 0, 0);
  lv_obj_set_style_bg_color(divider, lv_color_hex(0x49506a), 0);
  lv_obj_set_style_bg_opa(divider, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(divider, 0, 0);
  lv_obj_set_style_pad_all(divider, 0, 0);

  lv_obj_t *sun_disc = lv_obj_create(astronomy_card);
  lv_obj_set_size(sun_disc, 38, 38);
  lv_obj_set_pos(sun_disc, 8, 48);
  lv_obj_clear_flag(sun_disc, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_color(sun_disc, lv_color_hex(0xffd43b), 0);
  lv_obj_set_style_bg_opa(sun_disc, LV_OPA_COVER, 0);
  lv_obj_set_style_border_color(sun_disc, lv_color_hex(0xffec99), 0);
  lv_obj_set_style_border_width(sun_disc, 3, 0);
  lv_obj_set_style_radius(sun_disc, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_pad_all(sun_disc, 0, 0);

  lv_obj_t *sun_heading = lv_label_create(astronomy_card);
  lv_label_set_text(sun_heading, "Sun");
  lv_obj_set_style_text_color(sun_heading, lv_color_hex(0xffec99), 0);
  lv_obj_set_pos(sun_heading, 57, 28);
  lv_obj_t *sun_times = lv_label_create(astronomy_card);
  lv_label_set_text(sun_times, "Rise --:--\nSet  --:--");
  lv_obj_set_style_text_color(sun_times, lv_color_hex(0xd5e2ff), 0);
  lv_obj_set_pos(sun_times, 57, 50);

  lv_obj_t *moon_disc = lv_obj_create(astronomy_card);
  lv_obj_set_size(moon_disc, 42, 42);
  lv_obj_set_pos(moon_disc, 151, 46);
  lv_obj_clear_flag(moon_disc, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_clear_flag(moon_disc, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
  lv_obj_set_style_bg_color(moon_disc, lv_color_hex(0xe7eaf4), 0);
  lv_obj_set_style_bg_opa(moon_disc, LV_OPA_COVER, 0);
  lv_obj_set_style_border_color(moon_disc, lv_color_hex(0xaab2cb), 0);
  lv_obj_set_style_border_width(moon_disc, 1, 0);
  lv_obj_set_style_radius(moon_disc, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_pad_all(moon_disc, 0, 0);
  lv_obj_t *moon_shadow = lv_obj_create(moon_disc);
  lv_obj_set_size(moon_shadow, 42, 42);
  lv_obj_clear_flag(moon_shadow, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_color(moon_shadow, lv_color_hex(0x20243a), 0);
  lv_obj_set_style_bg_opa(moon_shadow, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(moon_shadow, 0, 0);
  lv_obj_set_style_radius(moon_shadow, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_pad_all(moon_shadow, 0, 0);

  lv_obj_t *moon_heading = lv_label_create(astronomy_card);
  lv_label_set_text(moon_heading, "Moon");
  lv_obj_set_style_text_color(moon_heading, lv_color_hex(0xdbe4ff), 0);
  lv_obj_set_pos(moon_heading, 204, 28);
  lv_obj_t *moon_phase = lv_label_create(astronomy_card);
  lv_obj_set_width(moon_phase, 82);
  lv_label_set_long_mode(moon_phase, LV_LABEL_LONG_WRAP);
  lv_label_set_text(moon_phase, "Moon data\nunavailable");
  lv_obj_set_style_text_color(moon_phase, lv_color_hex(0xd5e2ff), 0);
  lv_obj_set_pos(moon_phase, 204, 50);

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
  app_ctx.sun_times_label = sun_times;
  app_ctx.moon_phase_label = moon_phase;
  app_ctx.moon_shadow = moon_shadow;
  }

  lv_obj_t *pull_indicator = lv_label_create(scr);
  lv_label_set_text(pull_indicator, "Pull to refresh");
  lv_obj_set_style_text_color(pull_indicator, lv_color_hex(0xd7dde6), 0);
  lv_obj_set_style_bg_color(pull_indicator, lv_color_hex(0x14162a), 0);
  lv_obj_set_style_bg_opa(pull_indicator, LV_OPA_80, 0);
  lv_obj_set_style_pad_hor(pull_indicator, 10, 0);
  lv_obj_set_style_pad_ver(pull_indicator, 4, 0);
  lv_obj_set_style_radius(pull_indicator, LV_RADIUS_CIRCLE, 0);
  lv_obj_align(pull_indicator, LV_ALIGN_TOP_MID, 0, 50);
  lv_obj_add_flag(pull_indicator, LV_OBJ_FLAG_HIDDEN);

  app_ctx.screen = scr;
  app_ctx.pull_indicator = pull_indicator;
  app_ctx.loading_line = loading_line;
  app_ctx.refresh_timer = lv_timer_create(weather_app_timer_cb, 100, nullptr);
  lv_obj_add_event_cb(content, weather_pull_to_refresh_event, LV_EVENT_SCROLL, nullptr);
  lv_obj_add_event_cb(content, weather_pull_to_refresh_event, LV_EVENT_SCROLL_END, nullptr);

  weather_update_navigation(active_location);
  if (app_ctx.page_indicator) lv_obj_move_foreground(lv_obj_get_parent(app_ctx.page_indicator));
  lv_obj_update_layout(pager);
  lv_obj_scroll_to_x(pager, active_location * lv_obj_get_width(pager), LV_ANIM_OFF);
  lv_obj_add_event_cb(pager, weather_pager_event, LV_EVENT_SCROLL, nullptr);
  lv_obj_add_event_cb(pager, weather_pager_event, LV_EVENT_SCROLL_END, nullptr);

  WeatherService::instance().resume();
  app_ctx.was_fetching = WeatherService::instance().is_fetching();
  app_ctx.weather_revision = WeatherService::instance().data_revision();
  weather_app_update_ui();
  ui_load_screen(scr, false);
  weather_show_classic_intro();
  UI_TRACE("Weather", "launch loaded screen");
  
  // Set route after screen is loaded
  UiSessionState::set_route("weather");
}
