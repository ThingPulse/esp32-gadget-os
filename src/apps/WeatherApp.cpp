#include "apps/WeatherApp.h"

#include "AppRegistry.h"
#include "services/WeatherService.h"
#include "ui/StatusIcons.h"
#include "ui/WeatherIconSet.h"

#include <cstdio>
#include <string>

APP_REGISTER("Weather", LV_SYMBOL_EYE_OPEN, weather_app_launch);

namespace {

struct WeatherAppCtx {
  lv_obj_t *screen = nullptr;
  lv_obj_t *current_icon = nullptr;
  lv_obj_t *condition_label = nullptr;
  lv_obj_t *temp_label = nullptr;
  lv_obj_t *humidity_label = nullptr;
  lv_obj_t *pressure_label = nullptr;
  lv_obj_t *precip_label = nullptr;
  lv_obj_t *wind_label = nullptr;
  lv_obj_t *forecast_rows[WeatherService::FORECAST_DAYS]{};
  lv_obj_t *forecast_day_labels[WeatherService::FORECAST_DAYS]{};
  lv_obj_t *forecast_icons[WeatherService::FORECAST_DAYS]{};
  lv_obj_t *forecast_temp_labels[WeatherService::FORECAST_DAYS]{};
  lv_timer_t *refresh_timer = nullptr;
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

static std::string short_date(const std::string &iso)
{
  if (iso.size() >= 10) return iso.substr(5, 5);
  return iso;
}

static void nav_back_event(lv_event_t *e)
{
  if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
    lv_obj_t *target_scr = (lv_obj_t *)lv_event_get_user_data(e);
    lv_screen_load_anim(target_scr, LV_SCR_LOAD_ANIM_MOVE_RIGHT, 250, 0, true);
  }
}

static void weather_app_update_ui()
{
  if (!app_ctx.screen || !lv_obj_is_valid(app_ctx.screen)) return;

  WeatherService &weather = WeatherService::instance();

  if (!weather.has_weather()) {
    lv_label_set_text(app_ctx.condition_label, "No weather data yet");
    lv_label_set_text(app_ctx.temp_label, "Set location in Settings > Weather");
    lv_label_set_text(app_ctx.humidity_label, "Humidity: --");
    lv_label_set_text(app_ctx.pressure_label, "Pressure: --");
    lv_label_set_text(app_ctx.precip_label, "Precipitation: --");
    lv_label_set_text(app_ctx.wind_label, "Wind: --");

    for (int i = 0; i < WeatherService::FORECAST_DAYS; ++i) {
      if (app_ctx.forecast_rows[i])
        lv_obj_add_flag(app_ctx.forecast_rows[i], LV_OBJ_FLAG_HIDDEN);
    }
    return;
  }

  weather_icon_update(app_ctx.current_icon, weather_icon_kind_from_code(weather.weather_code()), 64);
  lv_label_set_text(app_ctx.condition_label, weather_text_for_code(weather.weather_code()));

  char temp_buf[64] = {0};
  std::snprintf(temp_buf, sizeof(temp_buf), "Temperature: %.1f %c", weather.temperature_value(), weather.is_metric() ? 'C' : 'F');
  lv_label_set_text(app_ctx.temp_label, temp_buf);

  char humidity_buf[64] = {0};
  std::snprintf(humidity_buf, sizeof(humidity_buf), "Humidity: %.0f%%", weather.humidity_value());
  lv_label_set_text(app_ctx.humidity_label, humidity_buf);

  char pressure_buf[64] = {0};
  std::snprintf(pressure_buf, sizeof(pressure_buf), "Pressure: %.0f hPa", weather.pressure_value());
  lv_label_set_text(app_ctx.pressure_label, pressure_buf);

  char precip_buf[64] = {0};
  std::snprintf(precip_buf, sizeof(precip_buf), "Precipitation: %.2f %s", weather.precipitation_value(), weather.is_metric() ? "mm" : "in");
  lv_label_set_text(app_ctx.precip_label, precip_buf);

  char wind_buf[96] = {0};
  std::snprintf(wind_buf,
                sizeof(wind_buf),
                "Wind: %.1f %s %s (%.0f deg)",
                weather.wind_speed_value(),
                weather.is_metric() ? "km/h" : "mph",
                wind_direction_short(weather.wind_direction_degrees()),
                weather.wind_direction_degrees());
  lv_label_set_text(app_ctx.wind_label, wind_buf);

  const WeatherForecastDay *forecast = weather.forecast_days();
  int count = weather.forecast_count();
  for (int i = 0; i < WeatherService::FORECAST_DAYS; ++i) {
    if (!app_ctx.forecast_rows[i]) continue;

    if (i >= count || !forecast[i].valid) {
      lv_obj_add_flag(app_ctx.forecast_rows[i], LV_OBJ_FLAG_HIDDEN);
      continue;
    }

    lv_obj_clear_flag(app_ctx.forecast_rows[i], LV_OBJ_FLAG_HIDDEN);
    std::string date_text = short_date(forecast[i].date);
    lv_label_set_text(app_ctx.forecast_day_labels[i], date_text.c_str());
    weather_icon_update(app_ctx.forecast_icons[i], weather_icon_kind_from_code(forecast[i].weather_code), 24);

    char day_temp_buf[64] = {0};
    std::snprintf(day_temp_buf,
                  sizeof(day_temp_buf),
                  "Min %.1f / Max %.1f %c",
                  forecast[i].temp_min,
                  forecast[i].temp_max,
                  weather.is_metric() ? 'C' : 'F');
    lv_label_set_text(app_ctx.forecast_temp_labels[i], day_temp_buf);
  }
}

static void weather_app_timer_cb(lv_timer_t *timer)
{
  (void)timer;
  weather_app_update_ui();
}

static void weather_app_screen_delete_event(lv_event_t *e)
{
  (void)e;
  if (app_ctx.refresh_timer) {
    lv_timer_delete(app_ctx.refresh_timer);
    app_ctx.refresh_timer = nullptr;
  }
  app_ctx = WeatherAppCtx{};
}

}  // namespace

void weather_app_launch(void)
{
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

  create_time_status_label(bar, 56);
  create_wifi_status_icon(bar, -8);

  lv_obj_t *content = lv_obj_create(scr);
  lv_obj_set_pos(content, 0, 44);
  lv_obj_set_size(content, lv_pct(100), 480 - 44);
  lv_obj_set_style_bg_opa(content, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(content, 0, 0);
  lv_obj_set_style_pad_all(content, 12, 0);
  lv_obj_set_style_pad_row(content, 10, 0);
  lv_obj_set_layout(content, LV_LAYOUT_FLEX);
  lv_obj_set_flex_flow(content, LV_FLEX_FLOW_COLUMN);

  lv_obj_t *current_row = lv_obj_create(content);
  lv_obj_set_size(current_row, lv_pct(100), 96);
  lv_obj_clear_flag(current_row, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_color(current_row, lv_color_hex(0x2a2d42), 0);
  lv_obj_set_style_bg_opa(current_row, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(current_row, 0, 0);
  lv_obj_set_style_radius(current_row, 10, 0);
  lv_obj_set_style_pad_all(current_row, 10, 0);

  lv_obj_t *current_icon = weather_icon_create(current_row, WeatherIconKind::Cloudy, 64);
  lv_obj_align(current_icon, LV_ALIGN_LEFT_MID, 0, 0);

  lv_obj_t *condition_label = lv_label_create(current_row);
  lv_obj_set_style_text_color(condition_label, lv_color_white(), 0);
  lv_obj_align(condition_label, LV_ALIGN_TOP_LEFT, 80, 8);

  lv_obj_t *temp_label = lv_label_create(current_row);
  lv_obj_set_style_text_color(temp_label, lv_color_hex(0xd5e2ff), 0);
  lv_obj_align(temp_label, LV_ALIGN_TOP_LEFT, 80, 38);

  lv_obj_t *metrics_card = lv_obj_create(content);
  lv_obj_set_size(metrics_card, lv_pct(100), 136);
  lv_obj_clear_flag(metrics_card, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_color(metrics_card, lv_color_hex(0x2a2d42), 0);
  lv_obj_set_style_bg_opa(metrics_card, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(metrics_card, 0, 0);
  lv_obj_set_style_radius(metrics_card, 10, 0);
  lv_obj_set_style_pad_all(metrics_card, 10, 0);
  lv_obj_set_style_pad_row(metrics_card, 6, 0);
  lv_obj_set_layout(metrics_card, LV_LAYOUT_FLEX);
  lv_obj_set_flex_flow(metrics_card, LV_FLEX_FLOW_COLUMN);

  lv_obj_t *humidity_label = lv_label_create(metrics_card);
  lv_obj_set_style_text_color(humidity_label, lv_color_white(), 0);

  lv_obj_t *pressure_label = lv_label_create(metrics_card);
  lv_obj_set_style_text_color(pressure_label, lv_color_white(), 0);

  lv_obj_t *precip_label = lv_label_create(metrics_card);
  lv_obj_set_style_text_color(precip_label, lv_color_white(), 0);

  lv_obj_t *wind_label = lv_label_create(metrics_card);
  lv_obj_set_style_text_color(wind_label, lv_color_white(), 0);

  lv_obj_t *forecast_title = lv_label_create(content);
  lv_label_set_text(forecast_title, "Next 3 Days");
  lv_obj_set_style_text_color(forecast_title, lv_color_hex(0xd5e2ff), 0);

  lv_obj_t *forecast_card = lv_obj_create(content);
  lv_obj_set_size(forecast_card, lv_pct(100), 150);
  lv_obj_clear_flag(forecast_card, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_color(forecast_card, lv_color_hex(0x2a2d42), 0);
  lv_obj_set_style_bg_opa(forecast_card, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(forecast_card, 0, 0);
  lv_obj_set_style_radius(forecast_card, 10, 0);
  lv_obj_set_style_pad_all(forecast_card, 8, 0);
  lv_obj_set_style_pad_row(forecast_card, 6, 0);
  lv_obj_set_layout(forecast_card, LV_LAYOUT_FLEX);
  lv_obj_set_flex_flow(forecast_card, LV_FLEX_FLOW_COLUMN);

  for (int i = 0; i < WeatherService::FORECAST_DAYS; ++i) {
    lv_obj_t *row = lv_obj_create(forecast_card);
    lv_obj_set_size(row, lv_pct(100), 40);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_pad_all(row, 0, 0);

    lv_obj_t *day_label = lv_label_create(row);
    lv_obj_set_style_text_color(day_label, lv_color_white(), 0);
    lv_obj_align(day_label, LV_ALIGN_LEFT_MID, 0, 0);

    lv_obj_t *icon = weather_icon_create(row, WeatherIconKind::Cloudy, 24);
    lv_obj_align(icon, LV_ALIGN_LEFT_MID, 62, 0);

    lv_obj_t *temp = lv_label_create(row);
    lv_obj_set_style_text_color(temp, lv_color_hex(0xd5e2ff), 0);
    lv_obj_align(temp, LV_ALIGN_RIGHT_MID, 0, 0);

    app_ctx.forecast_rows[i] = row;
    app_ctx.forecast_day_labels[i] = day_label;
    app_ctx.forecast_icons[i] = icon;
    app_ctx.forecast_temp_labels[i] = temp;
  }

  app_ctx.screen = scr;
  app_ctx.current_icon = current_icon;
  app_ctx.condition_label = condition_label;
  app_ctx.temp_label = temp_label;
  app_ctx.humidity_label = humidity_label;
  app_ctx.pressure_label = pressure_label;
  app_ctx.precip_label = precip_label;
  app_ctx.wind_label = wind_label;
  app_ctx.refresh_timer = lv_timer_create(weather_app_timer_cb, 5000, nullptr);

  weather_app_update_ui();
  lv_screen_load_anim(scr, LV_SCR_LOAD_ANIM_MOVE_LEFT, 250, 0, false);
}
