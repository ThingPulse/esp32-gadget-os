#include "apps/SettingsApp.h"
#include "AppRegistry.h"
#include "services/ConfigStoreService.h"
#include "services/PlanesService.h"
#include "services/TimeService.h"
#include "services/WeatherService.h"
#include "services/WifiService.h"
#include "ui/UiNav.h"
#include "ui/UiSessionState.h"
#include "ui/StatusIcons.h"
#include "ui/UiTrace.h"
#include <ArduinoJson.h>
#include <lvgl.h>

#include <cstdio>
#include <cstring>

#if defined(ARDUINO)
#include <HTTPClient.h>
#include <WiFiClient.h>
#include <WiFi.h>
#endif

APP_REGISTER_COLOR("Settings", LV_SYMBOL_SETTINGS, settings_launch, 0x495057);

// ── Shared helpers ────────────────────────────────────────────────────────

static lv_obj_t *route_restore_settings_screen = nullptr;

static void nav_to_home_screen(lv_event_t *e)
{
  if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
    UI_TRACE("Settings", "nav_to_home clicked");
    UiSessionState::set_route("home");
    lv_obj_t *target_scr = (lv_obj_t *)lv_event_get_user_data(e);
    UI_TRACE("Settings", "nav_to_home target valid=%d", (target_scr && lv_obj_is_valid(target_scr)) ? 1 : 0);
    if (!target_scr || !lv_obj_is_valid(target_scr)) return;
    ui_load_screen(target_scr, true);
  }
}

static void nav_to_settings_screen(lv_event_t *e)
{
  if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
    UI_TRACE("Settings", "nav_to_settings clicked");
    UiSessionState::set_route("settings");
    lv_obj_t *target_scr = (lv_obj_t *)lv_event_get_user_data(e);
    UI_TRACE("Settings", "nav_to_settings target valid=%d", (target_scr && lv_obj_is_valid(target_scr)) ? 1 : 0);
    if (!target_scr || !lv_obj_is_valid(target_scr)) return;
    ui_load_screen(target_scr, true);
  }
}

static void make_top_bar(lv_obj_t *scr, const char *title, lv_obj_t *back_target, bool back_to_home)
{
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
  lv_obj_add_event_cb(back_btn, back_to_home ? nav_to_home_screen : nav_to_settings_screen, LV_EVENT_CLICKED, back_target);

  lv_obj_t *back_label = lv_label_create(back_btn);
  lv_label_set_text(back_label, LV_SYMBOL_LEFT);
  lv_obj_set_style_text_color(back_label, lv_color_white(), 0);
  lv_obj_center(back_label);

  lv_obj_t *lbl = lv_label_create(bar);
  lv_label_set_text(lbl, title);
  lv_obj_set_style_text_color(lbl, lv_color_white(), 0);
  lv_obj_align(lbl, LV_ALIGN_CENTER, 0, 0);

  create_time_status_label(bar, 56);
  create_wifi_status_icon(bar, -8);
}

// ── WiFi screen ───────────────────────────────────────────────────────────

#define WIFI_KB_HEIGHT 200
#define WIFI_BAR_HEIGHT 44
#define WIFI_SCREEN_H 480

struct WifiCtx {
  lv_obj_t *content;
  lv_obj_t *kb;
  lv_obj_t *ssid_input;
  lv_obj_t *pass_input;
  lv_obj_t *settings_scr;
};
static WifiCtx wifi_ctx;

struct TimeCtx {
  lv_obj_t *settings_scr;
  lv_obj_t *region_roller;
  lv_obj_t *timezone_roller;
  lv_obj_t *fmt_switch;
  int selected_region_index;
  int selected_tz_index;
  std::string region_options_cache;
  std::string timezone_options_cache;
};
static TimeCtx time_ctx;

struct WeatherCtx {
  lv_obj_t *content;
  lv_obj_t *kb;
  lv_obj_t *settings_scr;
  lv_obj_t *lat_input;
  lv_obj_t *lon_input;
  lv_obj_t *unit_switch;
  lv_obj_t *guess_result_label;
};
static WeatherCtx weather_ctx;

struct PlanesCtx {
  lv_obj_t *content;
  lv_obj_t *kb;
  lv_obj_t *settings_scr;
  lv_obj_t *lat_input;
  lv_obj_t *lon_input;
  lv_obj_t *range_input;
  lv_obj_t *status_label;
  lv_obj_t *guess_result_label;
};
static PlanesCtx planes_ctx;

static const char *weather_coord_kb_map[] = {
  "1", "2", "3", LV_SYMBOL_BACKSPACE, "\n",
  "4", "5", "6", "+/-", "\n",
  "7", "8", "9", ".", "\n",
  ",", "0", LV_SYMBOL_OK, LV_SYMBOL_CLOSE, nullptr
};

static const lv_buttonmatrix_ctrl_t weather_coord_kb_ctrl[] = {
  (lv_buttonmatrix_ctrl_t)1, (lv_buttonmatrix_ctrl_t)1, (lv_buttonmatrix_ctrl_t)1, (lv_buttonmatrix_ctrl_t)(LV_KEYBOARD_CTRL_BUTTON_FLAGS | 1),
  (lv_buttonmatrix_ctrl_t)1, (lv_buttonmatrix_ctrl_t)1, (lv_buttonmatrix_ctrl_t)1, (lv_buttonmatrix_ctrl_t)1,
  (lv_buttonmatrix_ctrl_t)1, (lv_buttonmatrix_ctrl_t)1, (lv_buttonmatrix_ctrl_t)1, (lv_buttonmatrix_ctrl_t)1,
  (lv_buttonmatrix_ctrl_t)1, (lv_buttonmatrix_ctrl_t)1, (lv_buttonmatrix_ctrl_t)(LV_KEYBOARD_CTRL_BUTTON_FLAGS | 1), (lv_buttonmatrix_ctrl_t)(LV_KEYBOARD_CTRL_BUTTON_FLAGS | 1)
};

static std::string normalize_coord_text(const char *text)
{
  std::string out = text ? text : "";
  for (size_t i = 0; i < out.size(); ++i) {
    if (out[i] == ',') out[i] = '.';
  }
  return out;
}

static bool fetch_ip_location(double &lat, double &lon, std::string &city, std::string &region, std::string &error)
{
  std::string payload;

#if defined(ARDUINO)
  WiFiClient client;
  HTTPClient http;
  if (!http.begin(client, "http://ip-api.com/json")) {
    error = "HTTP begin failed";
    return false;
  }

  int code = http.GET();
  if (code != HTTP_CODE_OK) {
    error = "HTTP error";
    http.end();
    return false;
  }

  payload = http.getString().c_str();
  http.end();
#else
  FILE *pipe = popen("curl -s --max-time 8 \"http://ip-api.com/json\"", "r");
  if (!pipe) {
    error = "Cannot run curl";
    return false;
  }

  char buffer[256];
  while (fgets(buffer, sizeof(buffer), pipe))
    payload += buffer;

  int rc = pclose(pipe);
  if (rc != 0 || payload.empty()) {
    error = "Network request failed";
    return false;
  }
#endif

  JsonDocument doc;
  if (deserializeJson(doc, payload.c_str(), payload.size()) != DeserializationError::Ok) {
    error = "Bad JSON response";
    return false;
  }

  std::string status = doc["status"].as<std::string>();
  if (status != "success") {
    std::string msg = doc["message"].as<std::string>();
    error = msg.empty() ? "Lookup failed" : msg;
    return false;
  }

  lat = doc["lat"].as<double>();
  lon = doc["lon"].as<double>();
  city = doc["city"].as<std::string>();
  region = doc["regionName"].as<std::string>();

  return true;
}

static void weather_guess_location_event(lv_event_t *e)
{
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;

  if (!weather_ctx.lat_input || !weather_ctx.lon_input || !weather_ctx.guess_result_label) return;

  lv_label_set_text(weather_ctx.guess_result_label, "Guessing location...");
  lv_refr_now(nullptr);

  double lat = 0.0;
  double lon = 0.0;
  std::string city;
  std::string region;
  std::string error;

  if (!fetch_ip_location(lat, lon, city, region, error)) {
    std::string label = "Location guess failed";
    if (!error.empty()) {
      label += ": ";
      label += error;
    }
    lv_label_set_text(weather_ctx.guess_result_label, label.c_str());
    return;
  }

  char lat_buf[24] = {0};
  char lon_buf[24] = {0};
  std::snprintf(lat_buf, sizeof(lat_buf), "%.6f", lat);
  std::snprintf(lon_buf, sizeof(lon_buf), "%.6f", lon);
  lv_textarea_set_text(weather_ctx.lat_input, lat_buf);
  lv_textarea_set_text(weather_ctx.lon_input, lon_buf);

  std::string place = city;
  if (!region.empty()) {
    if (!place.empty()) place += ", ";
    place += region;
  }
  if (place.empty()) place = "unknown location";

  std::string label = "Guessed: ";
  label += place;
  lv_label_set_text(weather_ctx.guess_result_label, label.c_str());
}

static void planes_guess_location_event(lv_event_t *e)
{
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;

  if (!planes_ctx.lat_input || !planes_ctx.lon_input || !planes_ctx.guess_result_label) return;

  lv_label_set_text(planes_ctx.guess_result_label, "Guessing location...");
  lv_refr_now(nullptr);

  double lat = 0.0;
  double lon = 0.0;
  std::string city;
  std::string region;
  std::string error;

  if (!fetch_ip_location(lat, lon, city, region, error)) {
    std::string label = "Location guess failed";
    if (!error.empty()) {
      label += ": ";
      label += error;
    }
    lv_label_set_text(planes_ctx.guess_result_label, label.c_str());
    return;
  }

  char lat_buf[24] = {0};
  char lon_buf[24] = {0};
  std::snprintf(lat_buf, sizeof(lat_buf), "%.6f", lat);
  std::snprintf(lon_buf, sizeof(lon_buf), "%.6f", lon);
  lv_textarea_set_text(planes_ctx.lat_input, lat_buf);
  lv_textarea_set_text(planes_ctx.lon_input, lon_buf);

  std::string place = city;
  if (!region.empty()) {
    if (!place.empty()) place += ", ";
    place += region;
  }
  if (place.empty()) place = "unknown location";

  std::string label = "Guessed: ";
  label += place;
  lv_label_set_text(planes_ctx.guess_result_label, label.c_str());
}

static void wifi_save_event(lv_event_t *e)
{
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;

  ConfigStoreService &cfg = ConfigStoreService::instance();
  if (cfg.load("wifi")) {
    cfg.set("ssid", std::string(lv_textarea_get_text(wifi_ctx.ssid_input)));
    const char *password = lv_textarea_get_text(wifi_ctx.pass_input);
    if (password[0] != '\0')
      cfg.set("password", std::string(password));
    cfg.save();
    WifiService::instance().reload_from_config();
  }

  UiSessionState::set_route("settings");
  if (!wifi_ctx.settings_scr || !lv_obj_is_valid(wifi_ctx.settings_scr)) return;
  ui_load_screen(wifi_ctx.settings_scr, true);
}

static void wifi_ta_event(lv_event_t *e)
{
  lv_event_code_t code = lv_event_get_code(e);
  lv_obj_t *ta = (lv_obj_t *)lv_event_get_target(e);

  if (code == LV_EVENT_FOCUSED) {
    if (!wifi_ctx.kb || !lv_obj_is_valid(wifi_ctx.kb)) return;
    if (!wifi_ctx.content || !lv_obj_is_valid(wifi_ctx.content)) return;
    lv_obj_clear_flag(wifi_ctx.kb, LV_OBJ_FLAG_HIDDEN);
    lv_keyboard_set_textarea(wifi_ctx.kb, ta);
    lv_obj_set_height(wifi_ctx.content, WIFI_SCREEN_H - WIFI_BAR_HEIGHT - WIFI_KB_HEIGHT);
    lv_obj_scroll_to_view(ta, LV_ANIM_ON);
  } else if (code == LV_EVENT_DEFOCUSED) {
    if (!wifi_ctx.kb || !lv_obj_is_valid(wifi_ctx.kb)) return;
    if (!wifi_ctx.content || !lv_obj_is_valid(wifi_ctx.content)) return;
    lv_obj_add_flag(wifi_ctx.kb, LV_OBJ_FLAG_HIDDEN);
    lv_keyboard_set_textarea(wifi_ctx.kb, nullptr);
    lv_obj_set_height(wifi_ctx.content, WIFI_SCREEN_H - WIFI_BAR_HEIGHT);
  }
}

static void wifi_kb_event(lv_event_t *e)
{
  lv_event_code_t code = lv_event_get_code(e);
  if (code == LV_EVENT_READY || code == LV_EVENT_CANCEL) {
    if (!wifi_ctx.kb || !lv_obj_is_valid(wifi_ctx.kb)) return;
    if (!wifi_ctx.content || !lv_obj_is_valid(wifi_ctx.content)) return;
    lv_obj_t *ta = lv_keyboard_get_textarea(wifi_ctx.kb);
    if (ta) lv_obj_clear_state(ta, LV_STATE_FOCUSED);
    lv_keyboard_set_textarea(wifi_ctx.kb, nullptr);
    lv_obj_add_flag(wifi_ctx.kb, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_height(wifi_ctx.content, WIFI_SCREEN_H - WIFI_BAR_HEIGHT);
  }
}

static void open_wifi_screen(lv_event_t *e)
{
  lv_obj_t *settings_scr = nullptr;
  if (e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    settings_scr = (lv_obj_t *)lv_event_get_user_data(e);
  } else {
    settings_scr = route_restore_settings_screen;
  }
  if (!settings_scr) return;

  lv_obj_t *scr = lv_obj_create(nullptr);
  lv_obj_set_style_bg_color(scr, lv_color_hex(0x1e2030), 0);
  lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
  lv_obj_set_style_pad_all(scr, 0, 0);
  lv_obj_set_style_border_width(scr, 0, 0);

  make_top_bar(scr, "WiFi", settings_scr, false);

  lv_obj_t *content = lv_obj_create(scr);
  lv_obj_set_pos(content, 0, WIFI_BAR_HEIGHT);
  lv_obj_set_size(content, lv_pct(100), WIFI_SCREEN_H - WIFI_BAR_HEIGHT);
  lv_obj_set_style_bg_opa(content, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(content, 0, 0);
  lv_obj_set_style_pad_all(content, 16, 0);
  lv_obj_set_style_pad_row(content, 12, 0);
  lv_obj_set_layout(content, LV_LAYOUT_FLEX);
  lv_obj_set_flex_flow(content, LV_FLEX_FLOW_COLUMN);

  lv_obj_t *ssid_label = lv_label_create(content);
  lv_label_set_text(ssid_label, "SSID");
  lv_obj_set_style_text_color(ssid_label, lv_color_white(), 0);

  lv_obj_t *ssid_input = lv_textarea_create(content);
  lv_obj_set_width(ssid_input, lv_pct(100));
  lv_textarea_set_one_line(ssid_input, true);
  lv_textarea_set_placeholder_text(ssid_input, "Enter SSID");
  lv_obj_add_event_cb(ssid_input, wifi_ta_event, LV_EVENT_FOCUSED, nullptr);
  lv_obj_add_event_cb(ssid_input, wifi_ta_event, LV_EVENT_DEFOCUSED, nullptr);

  lv_obj_t *pass_label = lv_label_create(content);
  lv_label_set_text(pass_label, "Password");
  lv_obj_set_style_text_color(pass_label, lv_color_white(), 0);

  lv_obj_t *pass_input = lv_textarea_create(content);
  lv_obj_set_width(pass_input, lv_pct(100));
  lv_textarea_set_one_line(pass_input, true);
  lv_textarea_set_password_mode(pass_input, false);
  lv_textarea_set_placeholder_text(pass_input, "Enter password");
  lv_obj_add_event_cb(pass_input, wifi_ta_event, LV_EVENT_FOCUSED, nullptr);
  lv_obj_add_event_cb(pass_input, wifi_ta_event, LV_EVENT_DEFOCUSED, nullptr);

  ConfigStoreService &cfg = ConfigStoreService::instance();
  if (cfg.load("wifi")) {
    std::string ssid = cfg.get("ssid", "");
    lv_textarea_set_text(ssid_input, ssid.c_str());
  }

  lv_obj_t *save_btn = lv_button_create(content);
  lv_obj_set_width(save_btn, lv_pct(100));
  lv_obj_set_height(save_btn, 44);
  lv_obj_add_event_cb(save_btn, wifi_save_event, LV_EVENT_CLICKED, nullptr);

  lv_obj_t *save_label = lv_label_create(save_btn);
  lv_label_set_text(save_label, "Save");
  lv_obj_center(save_label);

  /* On-screen keyboard — hidden until a textarea is focused */
  lv_obj_t *kb = lv_keyboard_create(scr);
  lv_obj_set_size(kb, lv_pct(100), WIFI_KB_HEIGHT);
  lv_obj_align(kb, LV_ALIGN_BOTTOM_MID, 0, 0);
  lv_obj_add_flag(kb, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_event_cb(kb, wifi_kb_event, LV_EVENT_READY, nullptr);
  lv_obj_add_event_cb(kb, wifi_kb_event, LV_EVENT_CANCEL, nullptr);

  wifi_ctx.content = content;
  wifi_ctx.kb = kb;
  wifi_ctx.ssid_input = ssid_input;
  wifi_ctx.pass_input = pass_input;
  wifi_ctx.settings_scr = settings_scr;

  ui_load_screen(scr, false);
  
  // Set route after screen is loaded
  UiSessionState::set_route("settings_wifi");
}

static void time_save_event(lv_event_t *e)
{
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;

  bool use_24h = lv_obj_has_state(time_ctx.fmt_switch, LV_STATE_CHECKED);
  TimeService::instance().save_preferences(time_ctx.selected_tz_index, use_24h);

  UiSessionState::set_route("settings");
  if (!time_ctx.settings_scr || !lv_obj_is_valid(time_ctx.settings_scr)) return;
  ui_load_screen(time_ctx.settings_scr, true);
}

static void timezone_changed_event(lv_event_t *e)
{
  if (lv_event_get_code(e) != LV_EVENT_VALUE_CHANGED) return;
  lv_obj_t *roller = (lv_obj_t *)lv_event_get_target(e);
  int local_tz_index = (int)lv_roller_get_selected(roller);
  time_ctx.selected_tz_index = TimeService::timezone_global_index(time_ctx.selected_region_index, local_tz_index);
}

static void rebuild_timezone_roller(int region_index, int selected_local_tz_index)
{
  time_ctx.timezone_options_cache.clear();
  int count = TimeService::timezone_count_in_region(region_index);
  for (int i = 0; i < count; ++i) {
    if (i > 0) time_ctx.timezone_options_cache += "\n";
    time_ctx.timezone_options_cache += TimeService::timezone_name_in_region(region_index, i);
  }

  if (count <= 0) {
    time_ctx.timezone_options_cache = "UTC";
    count = 1;
  }

  if (selected_local_tz_index < 0) selected_local_tz_index = 0;
  if (selected_local_tz_index >= count) selected_local_tz_index = count - 1;

  lv_roller_set_options(time_ctx.timezone_roller, time_ctx.timezone_options_cache.c_str(), LV_ROLLER_MODE_NORMAL);
  lv_roller_set_visible_row_count(time_ctx.timezone_roller, 4);
  lv_roller_set_selected(time_ctx.timezone_roller, (uint16_t)selected_local_tz_index, LV_ANIM_OFF);
  time_ctx.selected_tz_index = TimeService::timezone_global_index(region_index, selected_local_tz_index);
}

static void region_changed_event(lv_event_t *e)
{
  if (lv_event_get_code(e) != LV_EVENT_VALUE_CHANGED) return;
  lv_obj_t *roller = (lv_obj_t *)lv_event_get_target(e);
  int region_index = (int)lv_roller_get_selected(roller);
  time_ctx.selected_region_index = region_index;
  rebuild_timezone_roller(region_index, 0);
}

static void open_time_screen(lv_event_t *e)
{
  lv_obj_t *settings_scr = nullptr;
  if (e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    settings_scr = (lv_obj_t *)lv_event_get_user_data(e);
  } else {
    settings_scr = route_restore_settings_screen;
  }
  if (!settings_scr) return;

  TimeService &time_service = TimeService::instance();

  lv_obj_t *scr = lv_obj_create(nullptr);
  lv_obj_set_style_bg_color(scr, lv_color_hex(0x1e2030), 0);
  lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
  lv_obj_set_style_pad_all(scr, 0, 0);
  lv_obj_set_style_border_width(scr, 0, 0);

  make_top_bar(scr, "Time", settings_scr, false);

  lv_obj_t *content = lv_obj_create(scr);
  lv_obj_set_pos(content, 0, WIFI_BAR_HEIGHT);
  lv_obj_set_size(content, lv_pct(100), WIFI_SCREEN_H - WIFI_BAR_HEIGHT);
  lv_obj_set_style_bg_opa(content, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(content, 0, 0);
  lv_obj_set_style_pad_all(content, 16, 0);
  lv_obj_set_style_pad_row(content, 12, 0);
  lv_obj_set_layout(content, LV_LAYOUT_FLEX);
  lv_obj_set_flex_flow(content, LV_FLEX_FLOW_COLUMN);

  lv_obj_t *tz_label = lv_label_create(content);
  lv_label_set_text(tz_label, "Region");
  lv_obj_set_style_text_color(tz_label, lv_color_white(), 0);

  lv_obj_t *region_roller = lv_roller_create(content);
  lv_obj_set_width(region_roller, lv_pct(100));
  lv_obj_set_height(region_roller, 120);
  if (time_ctx.region_options_cache.empty()) {
    for (int i = 0; i < TimeService::region_count(); ++i) {
      if (i > 0) time_ctx.region_options_cache += "\n";
      time_ctx.region_options_cache += TimeService::region_name(i);
    }
  }
  lv_roller_set_options(region_roller, time_ctx.region_options_cache.c_str(), LV_ROLLER_MODE_NORMAL);
  lv_roller_set_visible_row_count(region_roller, 4);

  lv_obj_t *tz_label2 = lv_label_create(content);
  lv_label_set_text(tz_label2, "Timezone");
  lv_obj_set_style_text_color(tz_label2, lv_color_white(), 0);

  lv_obj_t *tz_roller = lv_roller_create(content);
  lv_obj_set_width(tz_roller, lv_pct(100));
  lv_obj_set_height(tz_roller, 120);
  int region_index = 0;
  int local_tz_index = 0;
  TimeService::split_timezone_index(time_service.timezone_index(), &region_index, &local_tz_index);
  lv_roller_set_selected(region_roller, (uint16_t)region_index, LV_ANIM_OFF);

  time_ctx.timezone_roller = tz_roller;
  time_ctx.selected_region_index = region_index;
  rebuild_timezone_roller(region_index, local_tz_index);

  lv_obj_add_event_cb(region_roller, region_changed_event, LV_EVENT_VALUE_CHANGED, nullptr);
  lv_obj_add_event_cb(tz_roller, timezone_changed_event, LV_EVENT_VALUE_CHANGED, nullptr);

  lv_obj_t *fmt_row = lv_obj_create(content);
  lv_obj_set_size(fmt_row, lv_pct(100), 52);
  lv_obj_clear_flag(fmt_row, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_color(fmt_row, lv_color_hex(0x2a2d42), 0);
  lv_obj_set_style_bg_opa(fmt_row, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(fmt_row, 0, 0);
  lv_obj_set_style_radius(fmt_row, 10, 0);
  lv_obj_set_style_pad_hor(fmt_row, 16, 0);
  lv_obj_set_style_pad_ver(fmt_row, 0, 0);

  lv_obj_t *fmt_label = lv_label_create(fmt_row);
  lv_label_set_text(fmt_label, "24-hour format");
  lv_obj_set_style_text_color(fmt_label, lv_color_white(), 0);
  lv_obj_align(fmt_label, LV_ALIGN_LEFT_MID, 0, 0);

  lv_obj_t *fmt_switch = lv_switch_create(fmt_row);
  lv_obj_align(fmt_switch, LV_ALIGN_RIGHT_MID, 0, 0);
  if (time_service.use_24h())
    lv_obj_add_state(fmt_switch, LV_STATE_CHECKED);
  else
    lv_obj_clear_state(fmt_switch, LV_STATE_CHECKED);

  lv_obj_t *save_btn = lv_button_create(content);
  lv_obj_set_width(save_btn, lv_pct(100));
  lv_obj_set_height(save_btn, 44);
  lv_obj_add_event_cb(save_btn, time_save_event, LV_EVENT_CLICKED, nullptr);

  lv_obj_t *save_label = lv_label_create(save_btn);
  lv_label_set_text(save_label, "Save");
  lv_obj_center(save_label);

  time_ctx.settings_scr = settings_scr;
  time_ctx.region_roller = region_roller;
  time_ctx.timezone_roller = tz_roller;
  time_ctx.fmt_switch = fmt_switch;
  time_ctx.selected_region_index = region_index;
  time_ctx.selected_tz_index = time_service.timezone_index();

  ui_load_screen(scr, false);
  
  // Set route after screen is loaded
  UiSessionState::set_route("settings_time");
}

static void weather_ta_event(lv_event_t *e)
{
  lv_event_code_t code = lv_event_get_code(e);
  lv_obj_t *ta = (lv_obj_t *)lv_event_get_target(e);

  if (code == LV_EVENT_FOCUSED) {
    if (!weather_ctx.kb || !lv_obj_is_valid(weather_ctx.kb)) return;
    if (!weather_ctx.content || !lv_obj_is_valid(weather_ctx.content)) return;
    lv_obj_clear_flag(weather_ctx.kb, LV_OBJ_FLAG_HIDDEN);
    lv_keyboard_set_textarea(weather_ctx.kb, ta);
    lv_obj_set_height(weather_ctx.content, WIFI_SCREEN_H - WIFI_BAR_HEIGHT - WIFI_KB_HEIGHT);
    lv_obj_scroll_to_view(ta, LV_ANIM_ON);
  } else if (code == LV_EVENT_DEFOCUSED) {
    if (!weather_ctx.kb || !lv_obj_is_valid(weather_ctx.kb)) return;
    if (!weather_ctx.content || !lv_obj_is_valid(weather_ctx.content)) return;
    lv_obj_add_flag(weather_ctx.kb, LV_OBJ_FLAG_HIDDEN);
    lv_keyboard_set_textarea(weather_ctx.kb, nullptr);
    lv_obj_set_height(weather_ctx.content, WIFI_SCREEN_H - WIFI_BAR_HEIGHT);
  }
}

static void weather_kb_event(lv_event_t *e)
{
  lv_event_code_t code = lv_event_get_code(e);
  if (code == LV_EVENT_READY || code == LV_EVENT_CANCEL) {
    if (!weather_ctx.kb || !lv_obj_is_valid(weather_ctx.kb)) return;
    if (!weather_ctx.content || !lv_obj_is_valid(weather_ctx.content)) return;
    lv_obj_t *ta = lv_keyboard_get_textarea(weather_ctx.kb);
    if (ta) lv_obj_clear_state(ta, LV_STATE_FOCUSED);
    lv_keyboard_set_textarea(weather_ctx.kb, nullptr);
    lv_obj_add_flag(weather_ctx.kb, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_height(weather_ctx.content, WIFI_SCREEN_H - WIFI_BAR_HEIGHT);
  }
}

static void weather_save_event(lv_event_t *e)
{
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;

  ConfigStoreService &cfg = ConfigStoreService::instance();
  if (cfg.load("weather")) {
    cfg.set("latitude", normalize_coord_text(lv_textarea_get_text(weather_ctx.lat_input)));
    cfg.set("longitude", normalize_coord_text(lv_textarea_get_text(weather_ctx.lon_input)));
    cfg.set("metric", lv_obj_has_state(weather_ctx.unit_switch, LV_STATE_CHECKED));
    cfg.save();
    WeatherService::instance().reload_from_config();
  }

  UiSessionState::set_route("settings");
  if (!weather_ctx.settings_scr || !lv_obj_is_valid(weather_ctx.settings_scr)) return;
  ui_load_screen(weather_ctx.settings_scr, true);
}

static void open_weather_screen(lv_event_t *e)
{
  lv_obj_t *settings_scr = nullptr;
  if (e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    settings_scr = (lv_obj_t *)lv_event_get_user_data(e);
  } else {
    settings_scr = route_restore_settings_screen;
  }
  if (!settings_scr) return;

  lv_obj_t *scr = lv_obj_create(nullptr);
  lv_obj_set_style_bg_color(scr, lv_color_hex(0x1e2030), 0);
  lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
  lv_obj_set_style_pad_all(scr, 0, 0);
  lv_obj_set_style_border_width(scr, 0, 0);

  make_top_bar(scr, "Weather", settings_scr, false);

  lv_obj_t *content = lv_obj_create(scr);
  lv_obj_set_pos(content, 0, WIFI_BAR_HEIGHT);
  lv_obj_set_size(content, lv_pct(100), WIFI_SCREEN_H - WIFI_BAR_HEIGHT);
  lv_obj_set_style_bg_opa(content, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(content, 0, 0);
  lv_obj_set_style_pad_all(content, 16, 0);
  lv_obj_set_style_pad_row(content, 12, 0);
  lv_obj_set_layout(content, LV_LAYOUT_FLEX);
  lv_obj_set_flex_flow(content, LV_FLEX_FLOW_COLUMN);

  lv_obj_t *lat_label = lv_label_create(content);
  lv_label_set_text(lat_label, "Latitude");
  lv_obj_set_style_text_color(lat_label, lv_color_white(), 0);

  lv_obj_t *lat_input = lv_textarea_create(content);
  lv_obj_set_width(lat_input, lv_pct(100));
  lv_textarea_set_one_line(lat_input, true);
  lv_textarea_set_accepted_chars(lat_input, "0123456789-.,");
  lv_textarea_set_placeholder_text(lat_input, "47.37");
  lv_obj_add_event_cb(lat_input, weather_ta_event, LV_EVENT_FOCUSED, nullptr);
  lv_obj_add_event_cb(lat_input, weather_ta_event, LV_EVENT_DEFOCUSED, nullptr);

  lv_obj_t *lon_label = lv_label_create(content);
  lv_label_set_text(lon_label, "Longitude");
  lv_obj_set_style_text_color(lon_label, lv_color_white(), 0);

  lv_obj_t *lon_input = lv_textarea_create(content);
  lv_obj_set_width(lon_input, lv_pct(100));
  lv_textarea_set_one_line(lon_input, true);
  lv_textarea_set_accepted_chars(lon_input, "0123456789-.,");
  lv_textarea_set_placeholder_text(lon_input, "8.54");
  lv_obj_add_event_cb(lon_input, weather_ta_event, LV_EVENT_FOCUSED, nullptr);
  lv_obj_add_event_cb(lon_input, weather_ta_event, LV_EVENT_DEFOCUSED, nullptr);

  lv_obj_t *guess_btn = lv_button_create(content);
  lv_obj_set_width(guess_btn, lv_pct(100));
  lv_obj_set_height(guess_btn, 44);
  lv_obj_add_event_cb(guess_btn, weather_guess_location_event, LV_EVENT_CLICKED, nullptr);

  lv_obj_t *guess_label = lv_label_create(guess_btn);
  lv_label_set_text(guess_label, "Guess from IP");
  lv_obj_center(guess_label);

  lv_obj_t *guess_result_label = lv_label_create(content);
  lv_label_set_text(guess_result_label, "");
  lv_obj_set_style_text_color(guess_result_label, lv_color_hex(0xb7bdd7), 0);

  lv_obj_t *unit_row = lv_obj_create(content);
  lv_obj_set_size(unit_row, lv_pct(100), 52);
  lv_obj_clear_flag(unit_row, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_color(unit_row, lv_color_hex(0x2a2d42), 0);
  lv_obj_set_style_bg_opa(unit_row, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(unit_row, 0, 0);
  lv_obj_set_style_radius(unit_row, 10, 0);
  lv_obj_set_style_pad_hor(unit_row, 16, 0);
  lv_obj_set_style_pad_ver(unit_row, 0, 0);

  lv_obj_t *unit_label = lv_label_create(unit_row);
  lv_label_set_text(unit_label, "Metric (C)");
  lv_obj_set_style_text_color(unit_label, lv_color_white(), 0);
  lv_obj_align(unit_label, LV_ALIGN_LEFT_MID, 0, 0);

  lv_obj_t *unit_switch = lv_switch_create(unit_row);
  lv_obj_align(unit_switch, LV_ALIGN_RIGHT_MID, 0, 0);

  ConfigStoreService &cfg = ConfigStoreService::instance();
  if (cfg.load("weather")) {
    std::string lat = cfg.get("latitude", "");
    std::string lon = cfg.get("longitude", "");
    bool metric = cfg.get_bool("metric", true);
    lv_textarea_set_text(lat_input, lat.c_str());
    lv_textarea_set_text(lon_input, lon.c_str());
    if (metric)
      lv_obj_add_state(unit_switch, LV_STATE_CHECKED);
    else
      lv_obj_clear_state(unit_switch, LV_STATE_CHECKED);
  }

  lv_obj_t *save_btn = lv_button_create(content);
  lv_obj_set_width(save_btn, lv_pct(100));
  lv_obj_set_height(save_btn, 44);
  lv_obj_add_event_cb(save_btn, weather_save_event, LV_EVENT_CLICKED, nullptr);

  lv_obj_t *save_label = lv_label_create(save_btn);
  lv_label_set_text(save_label, "Save");
  lv_obj_center(save_label);

  lv_obj_t *kb = lv_keyboard_create(scr);
  lv_obj_set_size(kb, lv_pct(100), WIFI_KB_HEIGHT);
  lv_obj_align(kb, LV_ALIGN_BOTTOM_MID, 0, 0);
  lv_keyboard_set_map(kb, LV_KEYBOARD_MODE_USER_1, weather_coord_kb_map, weather_coord_kb_ctrl);
  lv_keyboard_set_mode(kb, LV_KEYBOARD_MODE_USER_1);
  lv_obj_add_flag(kb, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_event_cb(kb, weather_kb_event, LV_EVENT_READY, nullptr);
  lv_obj_add_event_cb(kb, weather_kb_event, LV_EVENT_CANCEL, nullptr);

  weather_ctx.content = content;
  weather_ctx.kb = kb;
  weather_ctx.settings_scr = settings_scr;
  weather_ctx.lat_input = lat_input;
  weather_ctx.lon_input = lon_input;
  weather_ctx.unit_switch = unit_switch;
  weather_ctx.guess_result_label = guess_result_label;

  ui_load_screen(scr, false);
  
  // Set route after screen is loaded
  UiSessionState::set_route("settings_weather");
}

static void planes_ta_event(lv_event_t *e)
{
  lv_event_code_t code = lv_event_get_code(e);
  lv_obj_t *ta = (lv_obj_t *)lv_event_get_target(e);

  if (code == LV_EVENT_FOCUSED) {
    if (!planes_ctx.kb || !lv_obj_is_valid(planes_ctx.kb)) return;
    if (!planes_ctx.content || !lv_obj_is_valid(planes_ctx.content)) return;
    lv_obj_clear_flag(planes_ctx.kb, LV_OBJ_FLAG_HIDDEN);
    lv_keyboard_set_textarea(planes_ctx.kb, ta);
    lv_obj_set_height(planes_ctx.content, WIFI_SCREEN_H - WIFI_BAR_HEIGHT - WIFI_KB_HEIGHT);
    lv_obj_scroll_to_view(ta, LV_ANIM_ON);
  } else if (code == LV_EVENT_DEFOCUSED) {
    if (!planes_ctx.kb || !lv_obj_is_valid(planes_ctx.kb)) return;
    if (!planes_ctx.content || !lv_obj_is_valid(planes_ctx.content)) return;
    lv_obj_add_flag(planes_ctx.kb, LV_OBJ_FLAG_HIDDEN);
    lv_keyboard_set_textarea(planes_ctx.kb, nullptr);
    lv_obj_set_height(planes_ctx.content, WIFI_SCREEN_H - WIFI_BAR_HEIGHT);
  }
}

static void planes_kb_event(lv_event_t *e)
{
  lv_event_code_t code = lv_event_get_code(e);
  if (code == LV_EVENT_READY || code == LV_EVENT_CANCEL) {
    if (!planes_ctx.kb || !lv_obj_is_valid(planes_ctx.kb)) return;
    if (!planes_ctx.content || !lv_obj_is_valid(planes_ctx.content)) return;
    lv_obj_t *ta = lv_keyboard_get_textarea(planes_ctx.kb);
    if (ta) lv_obj_clear_state(ta, LV_STATE_FOCUSED);
    lv_keyboard_set_textarea(planes_ctx.kb, nullptr);
    lv_obj_add_flag(planes_ctx.kb, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_height(planes_ctx.content, WIFI_SCREEN_H - WIFI_BAR_HEIGHT);
  }
}

static void planes_save_event(lv_event_t *e)
{
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;

  UI_TRACE("Settings", "planes_save_event clicked");

  ConfigStoreService &cfg = ConfigStoreService::instance();
  if (cfg.load("planes")) {
    cfg.set("center_latitude", normalize_coord_text(lv_textarea_get_text(planes_ctx.lat_input)));
    cfg.set("center_longitude", normalize_coord_text(lv_textarea_get_text(planes_ctx.lon_input)));
    cfg.set("range_km", normalize_coord_text(lv_textarea_get_text(planes_ctx.range_input)));
    cfg.save();
    PlanesService::instance().reload_from_config();
  }

  UiSessionState::set_route("settings");
  UI_TRACE("Settings", "planes_save_event return_to_settings valid=%d",
           (planes_ctx.settings_scr && lv_obj_is_valid(planes_ctx.settings_scr)) ? 1 : 0);
  if (!planes_ctx.settings_scr || !lv_obj_is_valid(planes_ctx.settings_scr)) return;
  ui_load_screen(planes_ctx.settings_scr, true);
}

static void open_planes_screen(lv_event_t *e)
{
  UI_TRACE("Settings", "open_planes_screen begin from_event=%d", e ? 1 : 0);
  lv_obj_t *settings_scr = nullptr;
  if (e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    settings_scr = (lv_obj_t *)lv_event_get_user_data(e);
  } else {
    settings_scr = route_restore_settings_screen;
  }
  if (!settings_scr) return;
  UI_TRACE("Settings", "open_planes_screen settings_scr valid=%d", lv_obj_is_valid(settings_scr) ? 1 : 0);

  lv_obj_t *scr = lv_obj_create(nullptr);
  lv_obj_set_style_bg_color(scr, lv_color_hex(0x1e2030), 0);
  lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
  lv_obj_set_style_pad_all(scr, 0, 0);
  lv_obj_set_style_border_width(scr, 0, 0);

  make_top_bar(scr, "Planes", settings_scr, false);

  lv_obj_t *content = lv_obj_create(scr);
  lv_obj_set_pos(content, 0, WIFI_BAR_HEIGHT);
  lv_obj_set_size(content, lv_pct(100), WIFI_SCREEN_H - WIFI_BAR_HEIGHT);
  lv_obj_set_style_bg_opa(content, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(content, 0, 0);
  lv_obj_set_style_pad_all(content, 16, 0);
  lv_obj_set_style_pad_row(content, 12, 0);
  lv_obj_set_layout(content, LV_LAYOUT_FLEX);
  lv_obj_set_flex_flow(content, LV_FLEX_FLOW_COLUMN);

  lv_obj_t *lat_label = lv_label_create(content);
  lv_label_set_text(lat_label, "Center Latitude");
  lv_obj_set_style_text_color(lat_label, lv_color_white(), 0);

  lv_obj_t *lat_input = lv_textarea_create(content);
  lv_obj_set_width(lat_input, lv_pct(100));
  lv_textarea_set_one_line(lat_input, true);
  lv_textarea_set_accepted_chars(lat_input, "0123456789-.,");
  lv_textarea_set_placeholder_text(lat_input, "47.37");
  lv_obj_add_event_cb(lat_input, planes_ta_event, LV_EVENT_FOCUSED, nullptr);
  lv_obj_add_event_cb(lat_input, planes_ta_event, LV_EVENT_DEFOCUSED, nullptr);

  lv_obj_t *lon_label = lv_label_create(content);
  lv_label_set_text(lon_label, "Center Longitude");
  lv_obj_set_style_text_color(lon_label, lv_color_white(), 0);

  lv_obj_t *lon_input = lv_textarea_create(content);
  lv_obj_set_width(lon_input, lv_pct(100));
  lv_textarea_set_one_line(lon_input, true);
  lv_textarea_set_accepted_chars(lon_input, "0123456789-.,");
  lv_textarea_set_placeholder_text(lon_input, "8.54");
  lv_obj_add_event_cb(lon_input, planes_ta_event, LV_EVENT_FOCUSED, nullptr);
  lv_obj_add_event_cb(lon_input, planes_ta_event, LV_EVENT_DEFOCUSED, nullptr);

  lv_obj_t *guess_btn = lv_button_create(content);
  lv_obj_set_width(guess_btn, lv_pct(100));
  lv_obj_set_height(guess_btn, 44);
  lv_obj_add_event_cb(guess_btn, planes_guess_location_event, LV_EVENT_CLICKED, nullptr);

  lv_obj_t *guess_label = lv_label_create(guess_btn);
  lv_label_set_text(guess_label, "Guess from IP");
  lv_obj_center(guess_label);

  lv_obj_t *guess_result_label = lv_label_create(content);
  lv_label_set_text(guess_result_label, "");
  lv_obj_set_style_text_color(guess_result_label, lv_color_hex(0xb7bdd7), 0);

  lv_obj_t *range_label = lv_label_create(content);
  lv_label_set_text(range_label, "Visibility Range (km)");
  lv_obj_set_style_text_color(range_label, lv_color_white(), 0);

  lv_obj_t *range_input = lv_textarea_create(content);
  lv_obj_set_width(range_input, lv_pct(100));
  lv_textarea_set_one_line(range_input, true);
  lv_textarea_set_accepted_chars(range_input, "0123456789.,");
  lv_textarea_set_placeholder_text(range_input, "30");
  lv_obj_add_event_cb(range_input, planes_ta_event, LV_EVENT_FOCUSED, nullptr);
  lv_obj_add_event_cb(range_input, planes_ta_event, LV_EVENT_DEFOCUSED, nullptr);

  lv_obj_t *status_label = lv_label_create(content);
  lv_label_set_text(status_label, "Used to compute OpenSky bounding box.");
  lv_obj_set_style_text_color(status_label, lv_color_hex(0xb7bdd7), 0);

  ConfigStoreService &cfg = ConfigStoreService::instance();
  if (cfg.load("planes")) {
    std::string lat = cfg.get("center_latitude", "");
    std::string lon = cfg.get("center_longitude", "");
    std::string range = cfg.get("range_km", "30");
    lv_textarea_set_text(lat_input, lat.c_str());
    lv_textarea_set_text(lon_input, lon.c_str());
    lv_textarea_set_text(range_input, range.c_str());
  }

  lv_obj_t *save_btn = lv_button_create(content);
  lv_obj_set_width(save_btn, lv_pct(100));
  lv_obj_set_height(save_btn, 44);
  lv_obj_add_event_cb(save_btn, planes_save_event, LV_EVENT_CLICKED, nullptr);

  lv_obj_t *save_label = lv_label_create(save_btn);
  lv_label_set_text(save_label, "Save");
  lv_obj_center(save_label);

  lv_obj_t *kb = lv_keyboard_create(scr);
  lv_obj_set_size(kb, lv_pct(100), WIFI_KB_HEIGHT);
  lv_obj_align(kb, LV_ALIGN_BOTTOM_MID, 0, 0);
  lv_keyboard_set_map(kb, LV_KEYBOARD_MODE_USER_1, weather_coord_kb_map, weather_coord_kb_ctrl);
  lv_keyboard_set_mode(kb, LV_KEYBOARD_MODE_USER_1);
  lv_obj_add_flag(kb, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_event_cb(kb, planes_kb_event, LV_EVENT_READY, nullptr);
  lv_obj_add_event_cb(kb, planes_kb_event, LV_EVENT_CANCEL, nullptr);

  planes_ctx.content = content;
  planes_ctx.kb = kb;
  planes_ctx.settings_scr = settings_scr;
  planes_ctx.lat_input = lat_input;
  planes_ctx.lon_input = lon_input;
  planes_ctx.range_input = range_input;
  planes_ctx.status_label = status_label;
  planes_ctx.guess_result_label = guess_result_label;

  ui_load_screen(scr, false);
  UI_TRACE("Settings", "open_planes_screen loaded");
  
  // Set route after screen is loaded
  UiSessionState::set_route("settings_planes");
}

static lv_obj_t *create_settings_screen(lv_obj_t *home_scr)
{
  lv_obj_t *scr = lv_obj_create(nullptr);
  lv_obj_set_style_bg_color(scr, lv_color_hex(0x1e2030), 0);
  lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
  lv_obj_set_style_pad_all(scr, 0, 0);
  lv_obj_set_style_border_width(scr, 0, 0);

  make_top_bar(scr, "Settings", home_scr, true);

  /* Settings list */
  lv_obj_t *list = lv_obj_create(scr);
  lv_obj_set_pos(list, 0, 44);
  lv_obj_set_size(list, lv_pct(100), LV_SIZE_CONTENT);
  lv_obj_set_style_bg_opa(list, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(list, 0, 0);
  lv_obj_set_style_pad_all(list, 12, 0);
  lv_obj_set_style_pad_row(list, 8, 0);
  lv_obj_set_layout(list, LV_LAYOUT_FLEX);
  lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);

  /* WiFi row */
  lv_obj_t *row = lv_obj_create(list);
  lv_obj_set_size(row, lv_pct(100), 52);
  lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_color(row, lv_color_hex(0x2a2d42), 0);
  lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(row, 0, 0);
  lv_obj_set_style_radius(row, 10, 0);
  lv_obj_set_style_pad_hor(row, 16, 0);
  lv_obj_set_style_pad_ver(row, 0, 0);
  lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(row, open_wifi_screen, LV_EVENT_CLICKED, scr);

  lv_obj_t *wifi_label = lv_label_create(row);
  lv_label_set_text(wifi_label, LV_SYMBOL_WIFI "  WiFi");
  lv_obj_set_style_text_color(wifi_label, lv_color_white(), 0);
  lv_obj_align(wifi_label, LV_ALIGN_LEFT_MID, 0, 0);

  lv_obj_t *chevron = lv_label_create(row);
  lv_label_set_text(chevron, LV_SYMBOL_RIGHT);
  lv_obj_set_style_text_color(chevron, lv_color_hex(0x888888), 0);
  lv_obj_align(chevron, LV_ALIGN_RIGHT_MID, 0, 0);

  /* Time row */
  lv_obj_t *time_row = lv_obj_create(list);
  lv_obj_set_size(time_row, lv_pct(100), 52);
  lv_obj_clear_flag(time_row, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_color(time_row, lv_color_hex(0x2a2d42), 0);
  lv_obj_set_style_bg_opa(time_row, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(time_row, 0, 0);
  lv_obj_set_style_radius(time_row, 10, 0);
  lv_obj_set_style_pad_hor(time_row, 16, 0);
  lv_obj_set_style_pad_ver(time_row, 0, 0);
  lv_obj_add_flag(time_row, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(time_row, open_time_screen, LV_EVENT_CLICKED, scr);

  lv_obj_t *time_label = lv_label_create(time_row);
  lv_label_set_text(time_label, LV_SYMBOL_BELL "  Time");
  lv_obj_set_style_text_color(time_label, lv_color_white(), 0);
  lv_obj_align(time_label, LV_ALIGN_LEFT_MID, 0, 0);

  lv_obj_t *time_chevron = lv_label_create(time_row);
  lv_label_set_text(time_chevron, LV_SYMBOL_RIGHT);
  lv_obj_set_style_text_color(time_chevron, lv_color_hex(0x888888), 0);
  lv_obj_align(time_chevron, LV_ALIGN_RIGHT_MID, 0, 0);

  /* Weather row */
  lv_obj_t *weather_row = lv_obj_create(list);
  lv_obj_set_size(weather_row, lv_pct(100), 52);
  lv_obj_clear_flag(weather_row, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_color(weather_row, lv_color_hex(0x2a2d42), 0);
  lv_obj_set_style_bg_opa(weather_row, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(weather_row, 0, 0);
  lv_obj_set_style_radius(weather_row, 10, 0);
  lv_obj_set_style_pad_hor(weather_row, 16, 0);
  lv_obj_set_style_pad_ver(weather_row, 0, 0);
  lv_obj_add_flag(weather_row, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(weather_row, open_weather_screen, LV_EVENT_CLICKED, scr);

  lv_obj_t *weather_label = lv_label_create(weather_row);
  lv_label_set_text(weather_label, LV_SYMBOL_EYE_OPEN "  Weather");
  lv_obj_set_style_text_color(weather_label, lv_color_white(), 0);
  lv_obj_align(weather_label, LV_ALIGN_LEFT_MID, 0, 0);

  lv_obj_t *weather_chevron = lv_label_create(weather_row);
  lv_label_set_text(weather_chevron, LV_SYMBOL_RIGHT);
  lv_obj_set_style_text_color(weather_chevron, lv_color_hex(0x888888), 0);
  lv_obj_align(weather_chevron, LV_ALIGN_RIGHT_MID, 0, 0);

  /* Planes row */
  lv_obj_t *planes_row = lv_obj_create(list);
  lv_obj_set_size(planes_row, lv_pct(100), 52);
  lv_obj_clear_flag(planes_row, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_color(planes_row, lv_color_hex(0x2a2d42), 0);
  lv_obj_set_style_bg_opa(planes_row, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(planes_row, 0, 0);
  lv_obj_set_style_radius(planes_row, 10, 0);
  lv_obj_set_style_pad_hor(planes_row, 16, 0);
  lv_obj_set_style_pad_ver(planes_row, 0, 0);
  lv_obj_add_flag(planes_row, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(planes_row, open_planes_screen, LV_EVENT_CLICKED, scr);

  lv_obj_t *planes_label = lv_label_create(planes_row);
  lv_label_set_text(planes_label, LV_SYMBOL_GPS "  Planes");
  lv_obj_set_style_text_color(planes_label, lv_color_white(), 0);
  lv_obj_align(planes_label, LV_ALIGN_LEFT_MID, 0, 0);

  lv_obj_t *planes_chevron = lv_label_create(planes_row);
  lv_label_set_text(planes_chevron, LV_SYMBOL_RIGHT);
  lv_obj_set_style_text_color(planes_chevron, lv_color_hex(0x888888), 0);
  lv_obj_align(planes_chevron, LV_ALIGN_RIGHT_MID, 0, 0);

  return scr;
}

void settings_launch_with_route(const char *route)
{
  UI_TRACE("Settings", "settings_launch_with_route route=%s", route ? route : "<null>");
  lv_obj_t *home_scr = lv_screen_active();
  lv_obj_t *settings_scr = create_settings_screen(home_scr);
  route_restore_settings_screen = settings_scr;

  if (!route || route[0] == '\0' || std::strcmp(route, "settings") == 0) {
    UiSessionState::set_route("settings");
    ui_load_screen(settings_scr, false);
    route_restore_settings_screen = nullptr;
    return;
  }

  if (std::strcmp(route, "settings_wifi") == 0) {
    open_wifi_screen(nullptr);
  } else if (std::strcmp(route, "settings_time") == 0) {
    open_time_screen(nullptr);
  } else if (std::strcmp(route, "settings_weather") == 0) {
    open_weather_screen(nullptr);
  } else if (std::strcmp(route, "settings_planes") == 0) {
    open_planes_screen(nullptr);
  } else {
    UiSessionState::set_route("settings");
    ui_load_screen(settings_scr, false);
  }

  route_restore_settings_screen = nullptr;
  UI_TRACE("Settings", "settings_launch_with_route done");
}

void settings_launch(void)
{
  UI_TRACE("Settings", "settings_launch begin");
  lv_obj_t *home_scr = lv_screen_active();
  lv_obj_t *scr = create_settings_screen(home_scr);

  ui_load_screen(scr, false);
  UI_TRACE("Settings", "settings_launch loaded");
  
  // Set route after screen is loaded
  UiSessionState::set_route("settings");
}
