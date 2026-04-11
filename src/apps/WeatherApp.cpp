#include "apps/WeatherApp.h"

#include "AppRegistry.h"
#include "services/WeatherService.h"
#include "ui/UiNav.h"
#include "ui/StatusIcons.h"
#include "ui/UiSessionState.h"
#include "ui/UiTrace.h"
#include "ui/WeatherIconSet.h"

#include <cstdio>
#include <ctime>
#include <string>

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
  lv_obj_t *refresh_btn = nullptr;
  lv_timer_t *refresh_timer = nullptr;
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

static const char *weather_token_for_code(int weather_code)
{
  if (weather_code == 0) return "SUN";
  if (weather_code <= 3) return "CLD";
  if (weather_code >= 45 && weather_code <= 48) return "FOG";
  if (weather_code >= 51 && weather_code <= 67) return "RAN";
  if (weather_code >= 71 && weather_code <= 77) return "SNW";
  if (weather_code >= 80 && weather_code <= 82) return "RAN";
  if (weather_code >= 95) return "STM";
  return "CLD";
}

static const char *wind_direction_short(float degrees)
{
  static const char *dirs[] = {"N", "NE", "E", "SE", "S", "SW", "W", "NW"};
  int index = (int)((degrees + 22.5f) / 45.0f) % 8;
  if (index < 0) index += 8;
  return dirs[index];
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
        lv_label_set_text(app_ctx.forecast_icons[i], weather_token_for_code(forecast[i].weather_code));
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
        lv_label_set_text(app_ctx.hourly_icons[i], weather_token_for_code(hourly_forecast[i].weather_code));
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
  weather_app_update_ui();
}

static void weather_app_refresh_event(lv_event_t *e)
{
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  if (app_ctx.closing) return;

  UI_TRACE("Weather", "refresh clicked");

  if (app_ctx.condition_label && lv_obj_is_valid(app_ctx.condition_label))
    lv_label_set_text(app_ctx.condition_label, "Refreshing...");

  WeatherService::instance().refresh_now();
  weather_app_update_ui();
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
  app_ctx.compact_list_icons = false;
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
  lv_obj_set_size(back_btn, 44, 36);
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

  lv_obj_t *content = lv_obj_create(scr);
  lv_obj_set_pos(content, 0, 44);
  lv_obj_set_size(content, lv_pct(100), 480 - 44);
  lv_obj_set_style_bg_opa(content, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(content, 0, 0);
  lv_obj_set_style_pad_all(content, 12, 0);
  lv_obj_set_style_pad_row(content, 8, 0);
  lv_obj_set_layout(content, LV_LAYOUT_FLEX);
  lv_obj_set_flex_flow(content, LV_FLEX_FLOW_COLUMN);

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
      icon = lv_label_create(col);
      if (icon) {
        lv_obj_set_style_text_color(icon, lv_color_hex(0xd7dde6), 0);
        lv_obj_set_style_text_font(icon, &lv_font_montserrat_14, 0);
        lv_label_set_text(icon, "CLD");
      }
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
      icon = lv_label_create(col);
      if (icon) {
        lv_obj_set_style_text_color(icon, lv_color_hex(0xd7dde6), 0);
        lv_obj_set_style_text_font(icon, &lv_font_montserrat_14, 0);
        lv_label_set_text(icon, "CLD");
      }
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

  app_ctx.screen = scr;
  app_ctx.date_label = date_label;
  app_ctx.current_icon = current_icon;
  app_ctx.condition_label = condition_label;
  app_ctx.temp_label = temp_label;
  app_ctx.humidity_label = humidity_label;
  app_ctx.pressure_label = pressure_label;
  app_ctx.precip_label = precip_label;
  app_ctx.wind_label = wind_label;
  app_ctx.refresh_btn = refresh_btn;
  app_ctx.refresh_timer = lv_timer_create(weather_app_timer_cb, 5000, nullptr);

  WeatherService::instance().resume();
  weather_app_update_ui();
  ui_load_screen(scr, false);
  UI_TRACE("Weather", "launch loaded screen");
  
  // Set route after screen is loaded
  UiSessionState::set_route("weather");
}
