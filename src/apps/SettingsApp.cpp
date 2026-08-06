#include "apps/SettingsApp.h"
#include "AppRegistry.h"
#include "SystemVersion.h"
#include "services/ConfigStoreService.h"
#include "services/PlanesService.h"
#include "services/TimeService.h"
#include "services/WeatherService.h"
#include "services/WeatherLocationSearchService.h"
#include "services/WeatherLocationStore.h"
#include "services/WifiService.h"
#include "ui/UiNav.h"
#include "ui/UiSessionState.h"
#include "ui/StatusIcons.h"
#include "ui/UiTrace.h"
#include "ui/DisplayPreferences.h"
#include <ArduinoJson.h>
#include <lvgl.h>

#include <cstdio>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

#if defined(ARDUINO)
#include <Esp.h>
#include <HTTPClient.h>
#include <WiFiClient.h>
#include <WiFi.h>
#endif

APP_REGISTER_COLOR_RANK("Settings", LV_SYMBOL_SETTINGS, settings_launch, 0x495057, 100);

// ── Shared helpers ────────────────────────────────────────────────────────

static lv_obj_t *route_restore_settings_screen = nullptr;

static void configure_settings_keyboard(lv_obj_t *keyboard);
static void settings_keyboard_value_event(lv_event_t *e);
static void make_top_bar(lv_obj_t *scr, const char *title, lv_obj_t *back_target, bool back_to_home);

static void format_memory_size(char *buffer, size_t buffer_size, size_t bytes)
{
  if (bytes >= 1024U * 1024U)
    std::snprintf(buffer, buffer_size, "%.1f MiB", (double)bytes / (1024.0 * 1024.0));
  else
    std::snprintf(buffer, buffer_size, "%.1f KiB", (double)bytes / 1024.0);
}

static lv_obj_t *add_system_info_row(lv_obj_t *content, const char *name, const char *value)
{
  lv_obj_t *row = lv_obj_create(content);
  // Leave enough vertical room when a longer value wraps to two lines.
  lv_obj_set_size(row, lv_pct(100), 76);
  lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_color(row, lv_color_hex(0x2a2d42), 0);
  lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(row, 0, 0);
  lv_obj_set_style_radius(row, 10, 0);
  lv_obj_set_style_pad_hor(row, 14, 0);
  lv_obj_set_style_pad_ver(row, 7, 0);

  lv_obj_t *name_label = lv_label_create(row);
  lv_label_set_text(name_label, name);
  lv_obj_set_style_text_color(name_label, lv_color_hex(0xadb5bd), 0);
  lv_obj_align(name_label, LV_ALIGN_TOP_LEFT, 0, 0);

  lv_obj_t *value_label = lv_label_create(row);
  lv_obj_set_width(value_label, lv_pct(100));
  lv_label_set_long_mode(value_label, LV_LABEL_LONG_DOT);
  lv_label_set_text(value_label, value);
  lv_obj_set_style_text_color(value_label, lv_color_white(), 0);
  lv_obj_align(value_label, LV_ALIGN_BOTTOM_LEFT, 0, 0);

  return value_label;
}

struct SystemInfoRefreshContext {
  lv_timer_t *timer = nullptr;
#if defined(ARDUINO) && defined(ESP_PLATFORM)
  lv_obj_t *internal_heap = nullptr;
  lv_obj_t *internal_heap_health = nullptr;
  lv_obj_t *psram = nullptr;
#endif
  lv_obj_t *lvgl_heap = nullptr;
  lv_obj_t *uptime = nullptr;
};

static void refresh_system_info(lv_timer_t *timer)
{
  auto *context = static_cast<SystemInfoRefreshContext *>(lv_timer_get_user_data(timer));
  if (!context) return;

  char value[96] = {0};
  char free_size[20], total_size[20], extra_size[20];
#if defined(ARDUINO) && defined(ESP_PLATFORM)
  format_memory_size(free_size, sizeof(free_size), ESP.getFreeHeap());
  format_memory_size(total_size, sizeof(total_size), ESP.getHeapSize());
  std::snprintf(value, sizeof(value), "%s free / %s total", free_size, total_size);
  lv_label_set_text(context->internal_heap, value);

  format_memory_size(free_size, sizeof(free_size), ESP.getMaxAllocHeap());
  format_memory_size(extra_size, sizeof(extra_size), ESP.getMinFreeHeap());
  std::snprintf(value, sizeof(value), "%s largest / %s minimum free", free_size, extra_size);
  lv_label_set_text(context->internal_heap_health, value);

  if (ESP.getPsramSize() > 0) {
    format_memory_size(free_size, sizeof(free_size), ESP.getFreePsram());
    format_memory_size(total_size, sizeof(total_size), ESP.getPsramSize());
    std::snprintf(value, sizeof(value), "%s free / %s total", free_size, total_size);
  } else {
    std::snprintf(value, sizeof(value), "Not available");
  }
  lv_label_set_text(context->psram, value);
#endif

  lv_mem_monitor_t memory{};
  lv_mem_monitor(&memory);
  format_memory_size(free_size, sizeof(free_size), memory.free_size);
  format_memory_size(total_size, sizeof(total_size), memory.total_size);
  std::snprintf(value, sizeof(value), "%s free / %s total, %u%% used",
                free_size, total_size, (unsigned)memory.used_pct);
  lv_label_set_text(context->lvgl_heap, value);

  std::snprintf(value, sizeof(value), "%lu seconds", (unsigned long)(lv_tick_get() / 1000U));
  lv_label_set_text(context->uptime, value);
}

static void delete_system_info_timer(lv_event_t *event)
{
  auto *context = static_cast<SystemInfoRefreshContext *>(lv_event_get_user_data(event));
  if (!context) return;
  if (context->timer) lv_timer_delete(context->timer);
  delete context;
}

static void open_system_screen(lv_event_t *e)
{
  lv_obj_t *settings_scr = route_restore_settings_screen;
  if (e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    settings_scr = (lv_obj_t *)lv_event_get_user_data(e);
  }
  if (!settings_scr || !lv_obj_is_valid(settings_scr)) return;

  lv_obj_t *scr = lv_obj_create(nullptr);
  lv_obj_set_style_bg_color(scr, lv_color_hex(0x1e2030), 0);
  lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
  lv_obj_set_style_pad_all(scr, 0, 0);
  lv_obj_set_style_border_width(scr, 0, 0);
  make_top_bar(scr, "System", settings_scr, false);

  lv_obj_t *content = lv_obj_create(scr);
  lv_obj_set_pos(content, 0, 44);
  lv_obj_set_size(content, lv_pct(100), 480 - 44);
  lv_obj_set_style_bg_opa(content, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(content, 0, 0);
  lv_obj_set_style_pad_all(content, 12, 0);
  lv_obj_set_style_pad_row(content, 8, 0);
  lv_obj_set_layout(content, LV_LAYOUT_FLEX);
  lv_obj_set_flex_flow(content, LV_FLEX_FLOW_COLUMN);

  add_system_info_row(content, "Gadget OS version", GADGET_OS_VERSION);
  add_system_info_row(content, "Build", __DATE__ " " __TIME__);

  auto *refresh_context = new SystemInfoRefreshContext();

  char value[96] = {0};
#if defined(ARDUINO) && defined(ESP_PLATFORM)
  std::snprintf(value, sizeof(value), "%s rev %u, %u cores @ %u MHz",
                ESP.getChipModel(), ESP.getChipRevision(), ESP.getChipCores(),
                (unsigned)ESP.getCpuFreqMHz());
  add_system_info_row(content, "Hardware", value);
  add_system_info_row(content, "ESP-IDF", ESP.getSdkVersion());

  char free_size[20], total_size[20], extra_size[20];
  format_memory_size(free_size, sizeof(free_size), ESP.getFreeHeap());
  format_memory_size(total_size, sizeof(total_size), ESP.getHeapSize());
  std::snprintf(value, sizeof(value), "%s free / %s total", free_size, total_size);
  refresh_context->internal_heap = add_system_info_row(content, "Internal heap", value);

  format_memory_size(free_size, sizeof(free_size), ESP.getMaxAllocHeap());
  format_memory_size(extra_size, sizeof(extra_size), ESP.getMinFreeHeap());
  std::snprintf(value, sizeof(value), "%s largest / %s minimum free", free_size, extra_size);
  refresh_context->internal_heap_health = add_system_info_row(content, "Internal heap health", value);

  if (ESP.getPsramSize() > 0) {
    format_memory_size(free_size, sizeof(free_size), ESP.getFreePsram());
    format_memory_size(total_size, sizeof(total_size), ESP.getPsramSize());
    std::snprintf(value, sizeof(value), "%s free / %s total", free_size, total_size);
  } else {
    std::snprintf(value, sizeof(value), "Not available");
  }
  refresh_context->psram = add_system_info_row(content, "PSRAM", value);

  format_memory_size(free_size, sizeof(free_size), ESP.getSketchSize());
  format_memory_size(total_size, sizeof(total_size), ESP.getFlashChipSize());
  std::snprintf(value, sizeof(value), "%s firmware / %s flash", free_size, total_size);
  add_system_info_row(content, "Flash", value);
#else
  add_system_info_row(content, "Hardware", "Native simulator");
  add_system_info_row(content, "Framework", "Host build");
#endif

  lv_mem_monitor_t memory{};
  lv_mem_monitor(&memory);
  char lvgl_free[20], lvgl_total[20];
  format_memory_size(lvgl_free, sizeof(lvgl_free), memory.free_size);
  format_memory_size(lvgl_total, sizeof(lvgl_total), memory.total_size);
  std::snprintf(value, sizeof(value), "%s free / %s total, %u%% used",
                lvgl_free, lvgl_total, (unsigned)memory.used_pct);
  refresh_context->lvgl_heap = add_system_info_row(content, "LVGL heap", value);

  std::snprintf(value, sizeof(value), "%lu seconds", (unsigned long)(lv_tick_get() / 1000U));
  refresh_context->uptime = add_system_info_row(content, "Uptime", value);

  refresh_context->timer = lv_timer_create(refresh_system_info, 1000, refresh_context);
  lv_obj_add_event_cb(scr, delete_system_info_timer, LV_EVENT_DELETE, refresh_context);

  ui_load_screen(scr, false);
  UiSessionState::set_route("settings_system");
}

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
  lv_obj_set_size(back_btn, 56, 44);
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
  lv_obj_t *scan_status;
  lv_obj_t *scan_results;
  lv_obj_t *scan_loading_line;
  lv_timer_t *scan_timer;
  unsigned long scan_started_ms;
  bool scan_in_progress;
  std::vector<std::string> scan_ssids;
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
  lv_obj_t *search_input;
  lv_obj_t *search_status;
  lv_obj_t *saved_rows[WeatherLocationStore::MAX_LOCATIONS]{};
  lv_obj_t *saved_labels[WeatherLocationStore::MAX_LOCATIONS]{};
  lv_obj_t *result_buttons[WeatherLocationStore::MAX_LOCATIONS]{};
  lv_obj_t *result_labels[WeatherLocationStore::MAX_LOCATIONS]{};
  lv_timer_t *search_timer = nullptr;
  unsigned long search_revision = 0;
  std::vector<WeatherLocation> locations;
  int active_index = 0;
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
  lv_obj_t *unit_switch;
  lv_obj_t *hide_ground_switch;
};
static PlanesCtx planes_ctx;

struct DisplayCtx {
  lv_obj_t *settings_scr;
  lv_obj_t *brightness_slider;
  lv_obj_t *brightness_value_label;
  lv_obj_t *rotation_dropdown;
  lv_obj_t *fps_switch;
};
static DisplayCtx display_ctx;

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
    const std::string previous_ssid = cfg.get("ssid", "");
    const std::string selected_ssid = lv_textarea_get_text(wifi_ctx.ssid_input);
    cfg.set("ssid", selected_ssid);
    const char *password = lv_textarea_get_text(wifi_ctx.pass_input);
    if (password[0] != '\0' || selected_ssid != previous_ssid)
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

static void wifi_select_scan_result_event(lv_event_t *e)
{
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  const int index = (int)(intptr_t)lv_event_get_user_data(e);
  if (index < 0 || index >= (int)wifi_ctx.scan_ssids.size()) return;

  lv_textarea_set_text(wifi_ctx.ssid_input, wifi_ctx.scan_ssids[index].c_str());
  lv_textarea_set_text(wifi_ctx.pass_input, "");
  lv_obj_add_state(wifi_ctx.pass_input, LV_STATE_FOCUSED);
  lv_obj_send_event(wifi_ctx.pass_input, LV_EVENT_FOCUSED, nullptr);
}

static void wifi_show_scan_results(const std::vector<std::string> &ssids,
                                   const std::vector<int> &signal_strengths,
                                   const std::vector<bool> &secured)
{
  wifi_ctx.scan_ssids = ssids;
  lv_obj_clean(wifi_ctx.scan_results);

  for (int i = 0; i < (int)ssids.size(); ++i) {
    lv_obj_t *button = lv_button_create(wifi_ctx.scan_results);
    lv_obj_set_size(button, lv_pct(100), 44);
    lv_obj_set_style_bg_color(button, lv_color_hex(0x2a2d42), 0);
    lv_obj_add_event_cb(button, wifi_select_scan_result_event, LV_EVENT_CLICKED,
                        (void *)(intptr_t)i);

    lv_obj_t *label = lv_label_create(button);
    lv_obj_set_width(label, lv_pct(100));
    lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
    char text[96] = {0};
    std::snprintf(text, sizeof(text), "%s   %d dBm%s",
                  ssids[i].c_str(), signal_strengths[i], secured[i] ? "   secured" : "   open");
    lv_label_set_text(label, text);
    lv_obj_align(label, LV_ALIGN_LEFT_MID, 0, 0);
  }

  if (ssids.empty())
    lv_label_set_text(wifi_ctx.scan_status, "No Wi-Fi networks found");
  else
    lv_label_set_text(wifi_ctx.scan_status, "Tap a network to select it");
}

static void wifi_finish_scan()
{
  wifi_ctx.scan_in_progress = false;
  if (wifi_ctx.scan_loading_line && lv_obj_is_valid(wifi_ctx.scan_loading_line))
    lv_obj_add_flag(wifi_ctx.scan_loading_line, LV_OBJ_FLAG_HIDDEN);
}

static void wifi_scan_timer_event(lv_timer_t *timer)
{
  (void)timer;
  if (!wifi_ctx.scan_in_progress) return;

#if defined(ARDUINO)
  const int count = WiFi.scanComplete();
  if (count == WIFI_SCAN_RUNNING) return;
  if (count == WIFI_SCAN_FAILED) {
    wifi_finish_scan();
    lv_label_set_text(wifi_ctx.scan_status, "Network scan failed. Tap Scan to retry.");
    WiFi.scanDelete();
    return;
  }

  std::vector<std::string> ssids;
  std::vector<int> strengths;
  std::vector<bool> secured;
  for (int i = 0; i < count && (int)ssids.size() < 10; ++i) {
    const std::string ssid = WiFi.SSID(i).c_str();
    if (ssid.empty()) continue;
    bool duplicate = false;
    for (const std::string &saved : ssids)
      if (saved == ssid) duplicate = true;
    if (duplicate) continue;
    ssids.push_back(ssid);
    strengths.push_back(WiFi.RSSI(i));
    secured.push_back(WiFi.encryptionType(i) != WIFI_AUTH_OPEN);
  }
  wifi_finish_scan();
  wifi_show_scan_results(ssids, strengths, secured);
  WiFi.scanDelete();
#else
  if (lv_tick_get() - wifi_ctx.scan_started_ms < 1000) return;
  wifi_finish_scan();
  wifi_show_scan_results({"Demo Wi-Fi", "Guest Network", "Office 5G"},
                         {-42, -61, -74}, {true, false, true});
#endif
}

static void wifi_scan_event(lv_event_t *e)
{
  if (lv_event_get_code(e) != LV_EVENT_CLICKED || wifi_ctx.scan_in_progress) return;

  if (wifi_ctx.kb && lv_obj_is_valid(wifi_ctx.kb)) {
    lv_keyboard_set_textarea(wifi_ctx.kb, nullptr);
    lv_obj_add_flag(wifi_ctx.kb, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_height(wifi_ctx.content, WIFI_SCREEN_H - WIFI_BAR_HEIGHT);
  }
  lv_obj_clean(wifi_ctx.scan_results);
  lv_label_set_text(wifi_ctx.scan_status, "Searching for Wi-Fi networks...");
  lv_obj_clear_flag(wifi_ctx.scan_loading_line, LV_OBJ_FLAG_HIDDEN);
  wifi_ctx.scan_in_progress = true;
  wifi_ctx.scan_started_ms = lv_tick_get();

#if defined(ARDUINO)
  WiFi.scanDelete();
  if (WiFi.scanNetworks(true) == WIFI_SCAN_FAILED) {
    wifi_finish_scan();
    lv_label_set_text(wifi_ctx.scan_status, "Could not start network scan");
  }
#endif
}

static void wifi_settings_delete_event(lv_event_t *e)
{
  (void)e;
  if (wifi_ctx.scan_timer) {
    lv_timer_delete(wifi_ctx.scan_timer);
    wifi_ctx.scan_timer = nullptr;
  }
#if defined(ARDUINO)
  if (wifi_ctx.scan_in_progress) WiFi.scanDelete();
#endif
  wifi_ctx.scan_in_progress = false;
  wifi_ctx.scan_ssids.clear();
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

  lv_obj_t *loading_line = lv_obj_create(scr);
  lv_obj_set_size(loading_line, 76, 3);
  lv_obj_set_pos(loading_line, -76, WIFI_BAR_HEIGHT - 3);
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

  lv_obj_t *scan_btn = lv_button_create(content);
  lv_obj_set_size(scan_btn, lv_pct(100), 42);
  lv_obj_add_event_cb(scan_btn, wifi_scan_event, LV_EVENT_CLICKED, nullptr);
  lv_obj_t *scan_btn_label = lv_label_create(scan_btn);
  lv_label_set_text(scan_btn_label, LV_SYMBOL_REFRESH "  Scan networks");
  lv_obj_center(scan_btn_label);

  lv_obj_t *scan_status = lv_label_create(content);
  lv_obj_set_width(scan_status, lv_pct(100));
  lv_label_set_long_mode(scan_status, LV_LABEL_LONG_WRAP);
  lv_label_set_text(scan_status, "Scan or enter an SSID manually");
  lv_obj_set_style_text_color(scan_status, lv_color_hex(0xb7bdd7), 0);

  lv_obj_t *scan_results = lv_obj_create(content);
  lv_obj_set_size(scan_results, lv_pct(100), LV_SIZE_CONTENT);
  lv_obj_set_style_bg_opa(scan_results, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(scan_results, 0, 0);
  lv_obj_set_style_pad_all(scan_results, 0, 0);
  lv_obj_set_style_pad_row(scan_results, 8, 0);
  lv_obj_set_layout(scan_results, LV_LAYOUT_FLEX);
  lv_obj_set_flex_flow(scan_results, LV_FLEX_FLOW_COLUMN);
  lv_obj_clear_flag(scan_results, LV_OBJ_FLAG_SCROLLABLE);

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
  lv_label_set_text(save_label, "Save & Back");
  lv_obj_center(save_label);

  /* On-screen keyboard — hidden until a textarea is focused */
  lv_obj_t *kb = lv_keyboard_create(scr);
  lv_obj_set_size(kb, lv_pct(100), WIFI_KB_HEIGHT);
  lv_obj_align(kb, LV_ALIGN_BOTTOM_MID, 0, 0);
  configure_settings_keyboard(kb);
  lv_obj_add_event_cb(kb, settings_keyboard_value_event, LV_EVENT_VALUE_CHANGED, nullptr);
  lv_obj_add_flag(kb, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_event_cb(kb, wifi_kb_event, LV_EVENT_READY, nullptr);
  lv_obj_add_event_cb(kb, wifi_kb_event, LV_EVENT_CANCEL, nullptr);

  wifi_ctx.content = content;
  wifi_ctx.kb = kb;
  wifi_ctx.ssid_input = ssid_input;
  wifi_ctx.pass_input = pass_input;
  wifi_ctx.settings_scr = settings_scr;
  wifi_ctx.scan_status = scan_status;
  wifi_ctx.scan_results = scan_results;
  wifi_ctx.scan_loading_line = loading_line;
  wifi_ctx.scan_in_progress = false;
  wifi_ctx.scan_started_ms = 0;
  wifi_ctx.scan_ssids.clear();
  wifi_ctx.scan_timer = lv_timer_create(wifi_scan_timer_event, 100, nullptr);
  lv_obj_add_event_cb(scr, wifi_settings_delete_event, LV_EVENT_DELETE, nullptr);

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
  lv_label_set_text(save_label, "Save & Back");
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

static void configure_settings_keyboard(lv_obj_t *keyboard)
{
  if (!keyboard || !lv_obj_is_valid(keyboard)) return;
  lv_keyboard_set_popovers(keyboard, true);
  const char * const *map = lv_buttonmatrix_get_map(keyboard);
  if (!map) return;
  uint32_t button_id = 0;
  for (uint32_t i = 0; map[i] && map[i][0] != '\0'; ++i) {
    if (std::strcmp(map[i], "\n") == 0) continue;
    if (std::strcmp(map[i], LV_SYMBOL_BACKSPACE) == 0)
      lv_buttonmatrix_clear_button_ctrl(keyboard, button_id, LV_BUTTONMATRIX_CTRL_NO_REPEAT);
    else
      lv_buttonmatrix_set_button_ctrl(keyboard, button_id, LV_BUTTONMATRIX_CTRL_NO_REPEAT);
    ++button_id;
  }
}

static void settings_keyboard_value_event(lv_event_t *e)
{
  // Reapply after a mode change because LVGL replaces the keyboard control map.
  configure_settings_keyboard((lv_obj_t *)lv_event_get_target(e));
}

static std::string weather_location_subtitle(const WeatherLocation &location)
{
  std::string text;
  if (!location.region.empty() && location.region != location.name) text = location.region;
  if (!location.country.empty()) {
    if (!text.empty()) text += ", ";
    text += location.country;
  }
  return text;
}

static void weather_update_location_rows()
{
  for (int i = 0; i < WeatherLocationStore::MAX_LOCATIONS; ++i) {
    if (!weather_ctx.saved_rows[i] || !lv_obj_is_valid(weather_ctx.saved_rows[i])) continue;
    if (i >= (int)weather_ctx.locations.size()) {
      lv_obj_add_flag(weather_ctx.saved_rows[i], LV_OBJ_FLAG_HIDDEN);
      continue;
    }
    lv_obj_clear_flag(weather_ctx.saved_rows[i], LV_OBJ_FLAG_HIDDEN);
    std::string label = i == weather_ctx.active_index ? LV_SYMBOL_OK " " : "";
    label += weather_ctx.locations[i].name;
    const std::string subtitle = weather_location_subtitle(weather_ctx.locations[i]);
    if (!subtitle.empty()) label += "\n" + subtitle;
    lv_label_set_text(weather_ctx.saved_labels[i], label.c_str());
  }
}

static void weather_select_location_event(lv_event_t *e)
{
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  const int index = (int)(intptr_t)lv_event_get_user_data(e);
  if (index < 0 || index >= (int)weather_ctx.locations.size()) return;
  weather_ctx.active_index = index;
  WeatherLocationStore::save(weather_ctx.locations, weather_ctx.active_index);
  WeatherService::instance().reload_from_config();
  weather_update_location_rows();
}

static void weather_delete_location_event(lv_event_t *e)
{
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  const int index = (int)(intptr_t)lv_event_get_user_data(e);
  if (index < 0 || index >= (int)weather_ctx.locations.size()) return;
  weather_ctx.locations.erase(weather_ctx.locations.begin() + index);
  if (weather_ctx.active_index > index) --weather_ctx.active_index;
  if (weather_ctx.active_index >= (int)weather_ctx.locations.size())
    weather_ctx.active_index = std::max(0, (int)weather_ctx.locations.size() - 1);
  WeatherLocationStore::save(weather_ctx.locations, weather_ctx.active_index);
  WeatherService::instance().reload_from_config();
  weather_update_location_rows();
}

static void weather_add_search_result_event(lv_event_t *e)
{
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  const int index = (int)(intptr_t)lv_event_get_user_data(e);
  const auto &results = WeatherLocationSearchService::instance().results();
  if (index < 0 || index >= (int)results.size()) return;
  if ((int)weather_ctx.locations.size() >= WeatherLocationStore::MAX_LOCATIONS) {
    lv_label_set_text(weather_ctx.search_status, "Five-location limit reached");
    return;
  }
  for (const WeatherLocation &saved : weather_ctx.locations) {
    if (std::fabs(saved.latitude - results[index].latitude) < 0.00001 &&
        std::fabs(saved.longitude - results[index].longitude) < 0.00001) {
      lv_label_set_text(weather_ctx.search_status, "Location already saved");
      return;
    }
  }
  weather_ctx.locations.push_back(results[index]);
  weather_ctx.active_index = (int)weather_ctx.locations.size() - 1;
  WeatherLocationStore::save(weather_ctx.locations, weather_ctx.active_index);
  WeatherService::instance().reload_from_config();
  weather_update_location_rows();
  lv_label_set_text(weather_ctx.search_status, "Location saved and selected");
}

static void weather_search_event(lv_event_t *e)
{
  if (lv_event_get_code(e) != LV_EVENT_CLICKED || !weather_ctx.search_input) return;
  lv_obj_t *spinner = (lv_obj_t *)lv_event_get_user_data(e);
  const char *query = lv_textarea_get_text(weather_ctx.search_input);
  if (!query || std::strlen(query) < 2) {
    lv_label_set_text(weather_ctx.search_status, "Enter at least two characters");
    return;
  }
  if (weather_ctx.kb && lv_obj_is_valid(weather_ctx.kb)) {
    lv_obj_add_flag(weather_ctx.kb, LV_OBJ_FLAG_HIDDEN);
    lv_keyboard_set_textarea(weather_ctx.kb, nullptr);
    lv_obj_set_height(weather_ctx.content, WIFI_SCREEN_H - WIFI_BAR_HEIGHT);
  }
  if (WeatherLocationSearchService::instance().search(query)) {
    lv_label_set_text(weather_ctx.search_status, "Searching...");
    if (spinner && lv_obj_is_valid(spinner)) lv_obj_clear_flag(spinner, LV_OBJ_FLAG_HIDDEN);
  }
}

static void weather_search_timer_event(lv_timer_t *timer)
{
  lv_obj_t *spinner = (lv_obj_t *)lv_timer_get_user_data(timer);
  WeatherLocationSearchService &search = WeatherLocationSearchService::instance();
  if (search.revision() == weather_ctx.search_revision) return;
  weather_ctx.search_revision = search.revision();
  if (spinner && lv_obj_is_valid(spinner)) lv_obj_add_flag(spinner, LV_OBJ_FLAG_HIDDEN);
  const auto &results = search.results();
  for (int i = 0; i < WeatherLocationStore::MAX_LOCATIONS; ++i) {
    if (!weather_ctx.result_buttons[i] || !lv_obj_is_valid(weather_ctx.result_buttons[i])) continue;
    if (i >= (int)results.size()) {
      lv_obj_add_flag(weather_ctx.result_buttons[i], LV_OBJ_FLAG_HIDDEN);
      continue;
    }
    lv_obj_clear_flag(weather_ctx.result_buttons[i], LV_OBJ_FLAG_HIDDEN);
    std::string label = results[i].name;
    const std::string subtitle = weather_location_subtitle(results[i]);
    if (!subtitle.empty()) label += "\n" + subtitle;
    lv_label_set_text(weather_ctx.result_labels[i], label.c_str());
  }
  lv_label_set_text(weather_ctx.search_status,
                    search.error().empty() ? "Tap a result to save it" : search.error().c_str());
}

static void weather_settings_delete_event(lv_event_t *e)
{
  (void)e;
  if (weather_ctx.search_timer) {
    lv_timer_delete(weather_ctx.search_timer);
    weather_ctx.search_timer = nullptr;
  }
}

static void weather_unit_changed_event(lv_event_t *e)
{
  if (lv_event_get_code(e) != LV_EVENT_VALUE_CHANGED) return;

  ConfigStoreService &cfg = ConfigStoreService::instance();
  if (cfg.load("weather")) {
    cfg.set("metric", lv_obj_has_state(weather_ctx.unit_switch, LV_STATE_CHECKED));
    cfg.save();
    WeatherService::instance().reload_from_config();
  }

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

  lv_obj_t *saved_title = lv_label_create(content);
  lv_label_set_text(saved_title, "Saved locations (tap to select)");
  lv_obj_set_style_text_color(saved_title, lv_color_white(), 0);

  for (int i = 0; i < WeatherLocationStore::MAX_LOCATIONS; ++i) {
    lv_obj_t *row = lv_obj_create(content);
    lv_obj_set_size(row, lv_pct(100), 54);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_pad_all(row, 0, 0);
    lv_obj_t *select = lv_button_create(row);
    lv_obj_set_size(select, 238, 50);
    lv_obj_align(select, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_set_style_bg_color(select, lv_color_hex(0x2a2d42), 0);
    lv_obj_add_event_cb(select, weather_select_location_event, LV_EVENT_CLICKED, (void *)(intptr_t)i);
    lv_obj_t *label = lv_label_create(select);
    lv_obj_set_width(label, 210);
    lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
    lv_obj_align(label, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_t *remove = lv_button_create(row);
    lv_obj_set_size(remove, 42, 42);
    lv_obj_align(remove, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_set_style_bg_color(remove, lv_color_hex(0x5c2b35), 0);
    lv_obj_add_event_cb(remove, weather_delete_location_event, LV_EVENT_CLICKED, (void *)(intptr_t)i);
    lv_obj_t *remove_label = lv_label_create(remove);
    lv_label_set_text(remove_label, LV_SYMBOL_TRASH);
    lv_obj_center(remove_label);
    weather_ctx.saved_rows[i] = row;
    weather_ctx.saved_labels[i] = label;
  }

  lv_obj_t *search_title = lv_label_create(content);
  lv_label_set_text(search_title, "Add a location");
  lv_obj_set_style_text_color(search_title, lv_color_white(), 0);
  lv_obj_t *search_input = lv_textarea_create(content);
  lv_obj_set_width(search_input, lv_pct(100));
  lv_textarea_set_one_line(search_input, true);
  lv_textarea_set_placeholder_text(search_input, "City or postal code");
  lv_obj_add_event_cb(search_input, weather_ta_event, LV_EVENT_FOCUSED, nullptr);
  lv_obj_add_event_cb(search_input, weather_ta_event, LV_EVENT_DEFOCUSED, nullptr);
  lv_obj_t *search_btn = lv_button_create(content);
  lv_obj_set_size(search_btn, lv_pct(100), 42);
  lv_obj_t *search_btn_label = lv_label_create(search_btn);
  lv_label_set_text(search_btn_label, "Search");
  lv_obj_center(search_btn_label);
  lv_obj_t *search_spinner = lv_spinner_create(search_btn);
  lv_obj_set_size(search_spinner, 20, 20);
  lv_obj_align(search_spinner, LV_ALIGN_RIGHT_MID, 0, 0);
  lv_obj_add_flag(search_spinner, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_event_cb(search_btn, weather_search_event, LV_EVENT_CLICKED, search_spinner);
  lv_obj_t *search_status = lv_label_create(content);
  lv_label_set_text(search_status, "Search uses Open-Meteo geocoding");
  lv_obj_set_width(search_status, lv_pct(100));
  lv_label_set_long_mode(search_status, LV_LABEL_LONG_WRAP);
  lv_obj_set_style_text_color(search_status, lv_color_hex(0xb7bdd7), 0);

  for (int i = 0; i < WeatherLocationStore::MAX_LOCATIONS; ++i) {
    lv_obj_t *button = lv_button_create(content);
    lv_obj_set_size(button, lv_pct(100), 50);
    lv_obj_set_style_bg_color(button, lv_color_hex(0x173827), 0);
    lv_obj_add_event_cb(button, weather_add_search_result_event, LV_EVENT_CLICKED, (void *)(intptr_t)i);
    lv_obj_add_flag(button, LV_OBJ_FLAG_HIDDEN);
    lv_obj_t *label = lv_label_create(button);
    lv_obj_set_width(label, lv_pct(100));
    lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
    lv_obj_align(label, LV_ALIGN_LEFT_MID, 0, 0);
    weather_ctx.result_buttons[i] = button;
    weather_ctx.result_labels[i] = label;
  }

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
    bool metric = cfg.get_bool("metric", true);
    if (metric)
      lv_obj_add_state(unit_switch, LV_STATE_CHECKED);
    else
      lv_obj_clear_state(unit_switch, LV_STATE_CHECKED);
  }
  lv_obj_add_event_cb(unit_switch, weather_unit_changed_event, LV_EVENT_VALUE_CHANGED, nullptr);

  lv_obj_t *autosave_label = lv_label_create(content);
  lv_label_set_text(autosave_label, "Changes are saved automatically");
  lv_obj_set_style_text_color(autosave_label, lv_color_hex(0xb7bdd7), 0);

  lv_obj_t *kb = lv_keyboard_create(scr);
  lv_obj_set_size(kb, lv_pct(100), WIFI_KB_HEIGHT);
  lv_obj_align(kb, LV_ALIGN_BOTTOM_MID, 0, 0);
  lv_keyboard_set_mode(kb, LV_KEYBOARD_MODE_TEXT_LOWER);
  configure_settings_keyboard(kb);
  lv_obj_add_event_cb(kb, settings_keyboard_value_event, LV_EVENT_VALUE_CHANGED, nullptr);
  lv_obj_add_flag(kb, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_event_cb(kb, weather_kb_event, LV_EVENT_READY, nullptr);
  lv_obj_add_event_cb(kb, weather_kb_event, LV_EVENT_CANCEL, nullptr);

  weather_ctx.content = content;
  weather_ctx.kb = kb;
  weather_ctx.settings_scr = settings_scr;
  weather_ctx.lat_input = nullptr;
  weather_ctx.lon_input = nullptr;
  weather_ctx.unit_switch = unit_switch;
  weather_ctx.guess_result_label = nullptr;
  weather_ctx.search_input = search_input;
  weather_ctx.search_status = search_status;
  WeatherLocationStore::load(weather_ctx.locations, weather_ctx.active_index);
  weather_update_location_rows();
  weather_ctx.search_revision = WeatherLocationSearchService::instance().revision();
  weather_ctx.search_timer = lv_timer_create(weather_search_timer_event, 100, search_spinner);
  lv_obj_add_event_cb(scr, weather_settings_delete_event, LV_EVENT_DELETE, nullptr);

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
    cfg.set("metric", lv_obj_has_state(planes_ctx.unit_switch, LV_STATE_CHECKED));
    cfg.set("hide_ground", lv_obj_has_state(planes_ctx.hide_ground_switch, LV_STATE_CHECKED));
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

  // Metric toggle
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
  lv_label_set_text(unit_label, "Metric units");
  lv_obj_set_style_text_color(unit_label, lv_color_white(), 0);
  lv_obj_align(unit_label, LV_ALIGN_LEFT_MID, 0, 0);
  lv_obj_t *unit_switch = lv_switch_create(unit_row);
  lv_obj_align(unit_switch, LV_ALIGN_RIGHT_MID, 0, 0);

  // Hide-on-ground toggle
  lv_obj_t *hg_row = lv_obj_create(content);
  lv_obj_set_size(hg_row, lv_pct(100), 52);
  lv_obj_clear_flag(hg_row, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_color(hg_row, lv_color_hex(0x2a2d42), 0);
  lv_obj_set_style_bg_opa(hg_row, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(hg_row, 0, 0);
  lv_obj_set_style_radius(hg_row, 10, 0);
  lv_obj_set_style_pad_hor(hg_row, 16, 0);
  lv_obj_set_style_pad_ver(hg_row, 0, 0);
  lv_obj_t *hg_label = lv_label_create(hg_row);
  lv_label_set_text(hg_label, "Hide planes on ground");
  lv_obj_set_style_text_color(hg_label, lv_color_white(), 0);
  lv_obj_align(hg_label, LV_ALIGN_LEFT_MID, 0, 0);
  lv_obj_t *hide_ground_switch = lv_switch_create(hg_row);
  lv_obj_align(hide_ground_switch, LV_ALIGN_RIGHT_MID, 0, 0);

  ConfigStoreService &cfg = ConfigStoreService::instance();
  if (cfg.load("planes")) {
    std::string lat = cfg.get("center_latitude", "");
    std::string lon = cfg.get("center_longitude", "");
    std::string range = cfg.get("range_km", "30");
    lv_textarea_set_text(lat_input, lat.c_str());
    lv_textarea_set_text(lon_input, lon.c_str());
    lv_textarea_set_text(range_input, range.c_str());
    if (cfg.get_bool("metric", true))
      lv_obj_add_state(unit_switch, LV_STATE_CHECKED);
    else
      lv_obj_clear_state(unit_switch, LV_STATE_CHECKED);
    if (cfg.get_bool("hide_ground", false))
      lv_obj_add_state(hide_ground_switch, LV_STATE_CHECKED);
    else
      lv_obj_clear_state(hide_ground_switch, LV_STATE_CHECKED);
  }

  lv_obj_t *save_btn = lv_button_create(content);
  lv_obj_set_width(save_btn, lv_pct(100));
  lv_obj_set_height(save_btn, 44);
  lv_obj_add_event_cb(save_btn, planes_save_event, LV_EVENT_CLICKED, nullptr);

  lv_obj_t *save_label = lv_label_create(save_btn);
  lv_label_set_text(save_label, "Save & Back");
  lv_obj_center(save_label);

  lv_obj_t *kb = lv_keyboard_create(scr);
  lv_obj_set_size(kb, lv_pct(100), WIFI_KB_HEIGHT);
  lv_obj_align(kb, LV_ALIGN_BOTTOM_MID, 0, 0);
  lv_keyboard_set_map(kb, LV_KEYBOARD_MODE_USER_1, weather_coord_kb_map, weather_coord_kb_ctrl);
  lv_keyboard_set_mode(kb, LV_KEYBOARD_MODE_USER_1);
  configure_settings_keyboard(kb);
  lv_obj_add_event_cb(kb, settings_keyboard_value_event, LV_EVENT_VALUE_CHANGED, nullptr);
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
  planes_ctx.unit_switch = unit_switch;
  planes_ctx.hide_ground_switch = hide_ground_switch;

  ui_load_screen(scr, false);
  UI_TRACE("Settings", "open_planes_screen loaded");
  
  // Set route after screen is loaded
  UiSessionState::set_route("settings_planes");
}

static void display_brightness_slider_event(lv_event_t *e)
{
  if (lv_event_get_code(e) != LV_EVENT_VALUE_CHANGED) return;
  lv_obj_t *slider = (lv_obj_t *)lv_event_get_target(e);
  lv_obj_t *value_label = (lv_obj_t *)lv_event_get_user_data(e);
  if (!slider || !value_label || !lv_obj_is_valid(slider) || !lv_obj_is_valid(value_label)) return;

  char brightness_text[8] = {0};
  std::snprintf(brightness_text, sizeof(brightness_text), "%d%%", (int)lv_slider_get_value(slider));
  lv_label_set_text(value_label, brightness_text);
}

static void display_save_event(lv_event_t *e)
{
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;

  if (!display_ctx.settings_scr || !lv_obj_is_valid(display_ctx.settings_scr)) return;
  if (!display_ctx.brightness_slider || !lv_obj_is_valid(display_ctx.brightness_slider)) return;
  if (!display_ctx.rotation_dropdown || !lv_obj_is_valid(display_ctx.rotation_dropdown)) return;
  if (!display_ctx.fps_switch || !lv_obj_is_valid(display_ctx.fps_switch)) return;

  int brightness_percent = (int)lv_slider_get_value(display_ctx.brightness_slider);
  uint16_t selected_rotation = (uint16_t)lv_dropdown_get_selected(display_ctx.rotation_dropdown);

  ConfigStoreService &cfg = ConfigStoreService::instance();
  if (cfg.load("display")) {
    cfg.set("brightness_percent", brightness_percent);
    cfg.set("rotation", (int)selected_rotation);
    cfg.set("show_fps", lv_obj_has_state(display_ctx.fps_switch, LV_STATE_CHECKED));
    cfg.save("display");
  }

  apply_display_preferences();

  ui_load_screen(display_ctx.settings_scr, true);
}

static void open_display_screen(lv_event_t *e)
{
  UI_TRACE("Settings", "open_display_screen event=%d", e ? (int)lv_event_get_code(e) : -1);
  lv_obj_t *settings_scr = route_restore_settings_screen;
  if (e) settings_scr = (lv_obj_t *)lv_event_get_user_data(e);
  if (!settings_scr || !lv_obj_is_valid(settings_scr)) return;

  lv_obj_t *scr = lv_obj_create(nullptr);
  lv_obj_set_style_bg_color(scr, lv_color_hex(0x1e2030), 0);
  lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
  lv_obj_set_style_pad_all(scr, 0, 0);
  lv_obj_set_style_border_width(scr, 0, 0);

  make_top_bar(scr, "Display", settings_scr, false);

  lv_obj_t *content = lv_obj_create(scr);
  lv_obj_set_pos(content, 0, 44);
  lv_obj_set_size(content, lv_pct(100), 480 - 44);
  lv_obj_set_style_bg_opa(content, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(content, 0, 0);
  lv_obj_set_style_pad_all(content, 16, 0);
  lv_obj_set_style_pad_row(content, 12, 0);
  lv_obj_set_layout(content, LV_LAYOUT_FLEX);
  lv_obj_set_flex_flow(content, LV_FLEX_FLOW_COLUMN);

  lv_obj_t *brightness_title = lv_label_create(content);
  lv_label_set_text(brightness_title, "Brightness");
  lv_obj_set_style_text_color(brightness_title, lv_color_white(), 0);

  lv_obj_t *brightness_row = lv_obj_create(content);
  lv_obj_set_size(brightness_row, lv_pct(100), 64);
  lv_obj_clear_flag(brightness_row, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_color(brightness_row, lv_color_hex(0x2a2d42), 0);
  lv_obj_set_style_bg_opa(brightness_row, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(brightness_row, 0, 0);
  lv_obj_set_style_radius(brightness_row, 10, 0);
  lv_obj_set_style_pad_hor(brightness_row, 14, 0);
  lv_obj_set_style_pad_ver(brightness_row, 8, 0);

  lv_obj_t *brightness_slider = lv_slider_create(brightness_row);
  lv_obj_set_width(brightness_slider, lv_pct(78));
  lv_obj_align(brightness_slider, LV_ALIGN_LEFT_MID, 0, 0);
  lv_slider_set_range(brightness_slider, 5, 100);

  lv_obj_t *brightness_value_label = lv_label_create(brightness_row);
  lv_label_set_text(brightness_value_label, "100%");
  lv_obj_set_style_text_color(brightness_value_label, lv_color_white(), 0);
  lv_obj_align(brightness_value_label, LV_ALIGN_RIGHT_MID, 0, 0);
  lv_obj_add_event_cb(brightness_slider, display_brightness_slider_event, LV_EVENT_VALUE_CHANGED, brightness_value_label);

  lv_obj_t *rotation_title = lv_label_create(content);
  lv_label_set_text(rotation_title, "Screen Rotation");
  lv_obj_set_style_text_color(rotation_title, lv_color_white(), 0);

  lv_obj_t *rotation_dropdown = lv_dropdown_create(content);
  lv_obj_set_width(rotation_dropdown, lv_pct(100));
  lv_dropdown_set_options(rotation_dropdown, "0\xb0 (Portrait)\n90\xb0 (Landscape)\n180\xb0 (Portrait Inverted)\n270\xb0 (Landscape Inverted)");

  lv_obj_t *fps_row = lv_obj_create(content);
  lv_obj_set_size(fps_row, lv_pct(100), 56);
  lv_obj_clear_flag(fps_row, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_color(fps_row, lv_color_hex(0x2a2d42), 0);
  lv_obj_set_style_bg_opa(fps_row, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(fps_row, 0, 0);
  lv_obj_set_style_radius(fps_row, 10, 0);
  lv_obj_set_style_pad_hor(fps_row, 14, 0);

  lv_obj_t *fps_label = lv_label_create(fps_row);
  lv_label_set_text(fps_label, "Show FPS monitor");
  lv_obj_set_style_text_color(fps_label, lv_color_white(), 0);
  lv_obj_align(fps_label, LV_ALIGN_LEFT_MID, 0, 0);

  lv_obj_t *fps_switch = lv_switch_create(fps_row);
  lv_obj_align(fps_switch, LV_ALIGN_RIGHT_MID, 0, 0);

  ConfigStoreService &cfg = ConfigStoreService::instance();
  int brightness_percent = 100;
  int rotation = 0;
  bool show_fps = false;
  if (cfg.load("display")) {
    brightness_percent = cfg.get_int("brightness_percent", 100);
    rotation = cfg.get_int("rotation", 0);
    show_fps = cfg.get_bool("show_fps", false);
  }
  if (brightness_percent < 5) brightness_percent = 5;
  if (brightness_percent > 100) brightness_percent = 100;
  if (rotation < 0 || rotation > 3) rotation = 0;

  lv_slider_set_value(brightness_slider, brightness_percent, LV_ANIM_OFF);
  char brightness_text[8] = {0};
  std::snprintf(brightness_text, sizeof(brightness_text), "%d%%", brightness_percent);
  lv_label_set_text(brightness_value_label, brightness_text);
  lv_dropdown_set_selected(rotation_dropdown, (uint16_t)rotation);
  if (show_fps)
    lv_obj_add_state(fps_switch, LV_STATE_CHECKED);

  lv_obj_t *save_btn = lv_button_create(content);
  lv_obj_set_width(save_btn, lv_pct(100));
  lv_obj_set_height(save_btn, 44);
  lv_obj_add_event_cb(save_btn, display_save_event, LV_EVENT_CLICKED, nullptr);

  lv_obj_t *save_label = lv_label_create(save_btn);
  lv_label_set_text(save_label, "Save & Back");
  lv_obj_center(save_label);

  display_ctx.settings_scr = settings_scr;
  display_ctx.brightness_slider = brightness_slider;
  display_ctx.brightness_value_label = brightness_value_label;
  display_ctx.rotation_dropdown = rotation_dropdown;
  display_ctx.fps_switch = fps_switch;

  ui_load_screen(scr, false);
  UI_TRACE("Settings", "open_display_screen loaded");

  UiSessionState::set_route("settings_display");
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
  lv_obj_set_size(list, lv_pct(100), 480 - 44);
  lv_obj_set_style_bg_opa(list, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(list, 0, 0);
  lv_obj_set_style_pad_all(list, 12, 0);
  lv_obj_set_style_pad_row(list, 8, 0);
  lv_obj_set_layout(list, LV_LAYOUT_FLEX);
  lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);

  /* System row */
  lv_obj_t *system_row = lv_obj_create(list);
  lv_obj_set_size(system_row, lv_pct(100), 52);
  lv_obj_clear_flag(system_row, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_color(system_row, lv_color_hex(0x2a2d42), 0);
  lv_obj_set_style_bg_opa(system_row, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(system_row, 0, 0);
  lv_obj_set_style_radius(system_row, 10, 0);
  lv_obj_set_style_pad_hor(system_row, 16, 0);
  lv_obj_set_style_pad_ver(system_row, 0, 0);
  lv_obj_add_flag(system_row, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(system_row, open_system_screen, LV_EVENT_CLICKED, scr);

  lv_obj_t *system_label = lv_label_create(system_row);
  lv_label_set_text(system_label, LV_SYMBOL_SETTINGS "  System");
  lv_obj_set_style_text_color(system_label, lv_color_white(), 0);
  lv_obj_align(system_label, LV_ALIGN_LEFT_MID, 0, 0);

  lv_obj_t *system_chevron = lv_label_create(system_row);
  lv_label_set_text(system_chevron, LV_SYMBOL_RIGHT);
  lv_obj_set_style_text_color(system_chevron, lv_color_hex(0x888888), 0);
  lv_obj_align(system_chevron, LV_ALIGN_RIGHT_MID, 0, 0);

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

  /* Display row */
  lv_obj_t *display_row = lv_obj_create(list);
  lv_obj_set_size(display_row, lv_pct(100), 52);
  lv_obj_clear_flag(display_row, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_color(display_row, lv_color_hex(0x2a2d42), 0);
  lv_obj_set_style_bg_opa(display_row, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(display_row, 0, 0);
  lv_obj_set_style_radius(display_row, 10, 0);
  lv_obj_set_style_pad_hor(display_row, 16, 0);
  lv_obj_set_style_pad_ver(display_row, 0, 0);
  lv_obj_add_flag(display_row, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(display_row, open_display_screen, LV_EVENT_CLICKED, scr);

  lv_obj_t *display_label = lv_label_create(display_row);
  lv_label_set_text(display_label, LV_SYMBOL_IMAGE "  Display");
  lv_obj_set_style_text_color(display_label, lv_color_white(), 0);
  lv_obj_align(display_label, LV_ALIGN_LEFT_MID, 0, 0);

  lv_obj_t *display_chevron = lv_label_create(display_row);
  lv_label_set_text(display_chevron, LV_SYMBOL_RIGHT);
  lv_obj_set_style_text_color(display_chevron, lv_color_hex(0x888888), 0);
  lv_obj_align(display_chevron, LV_ALIGN_RIGHT_MID, 0, 0);

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

  if (std::strcmp(route, "settings_system") == 0) {
    open_system_screen(nullptr);
  } else if (std::strcmp(route, "settings_wifi") == 0) {
    open_wifi_screen(nullptr);
  } else if (std::strcmp(route, "settings_time") == 0) {
    open_time_screen(nullptr);
  } else if (std::strcmp(route, "settings_weather") == 0) {
    open_weather_screen(nullptr);
  } else if (std::strcmp(route, "settings_planes") == 0) {
    open_planes_screen(nullptr);
  } else if (std::strcmp(route, "settings_display") == 0) {
    open_display_screen(nullptr);
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
