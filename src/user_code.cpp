#if __has_include(<LovyanGFX.h>)
#define LGFX_AUTODETECT
#include <LovyanGFX.h>
#include <LGFX_AUTODETECT.hpp>
#else
#include <cstdint>
namespace lgfx {
struct rgb565_t {
  uint16_t raw;
};
struct touch_point_t {
  int32_t x = 0;
  int32_t y = 0;
};
} // namespace lgfx

class LGFX {
 public:
  LGFX(int32_t w = 320, int32_t h = 480, int32_t = 0) : width_(w), height_(h) {}
  void init() {}
  void setRotation(int) {}
  int32_t width() const { return width_; }
  int32_t height() const { return height_; }
  void setBrightness(uint8_t) {}
  bool getTouch(lgfx::touch_point_t *) { return false; }
  void startWrite() {}
  void setAddrWindow(int32_t, int32_t, int32_t, int32_t) {}
  void writePixels(lgfx::rgb565_t *, int32_t) {}
  void endWrite() {}

 private:
  int32_t width_ = 320;
  int32_t height_ = 480;
};
#endif
#include <lvgl.h>
#include "AppRegistry.h"
#include "EventBus.h"
#include "ServiceRegistry.h"
#include "apps/CalculatorApp.h"
#include "apps/PlanesApp.h"
#include "apps/SettingsApp.h"
#include "apps/WeatherApp.h"
#include "apps/WeatherIconTestApp.h"
#include "assets/ThingPulseLogo.h"
#include "services/ConfigStoreService.h"
#include "services/TimeService.h"
#include "services/WeatherService.h"
#include "services/WeatherLocationStore.h"
#include "services/UpdateService.h"
#include "ui/UiSessionState.h"
#include "ui/StatusIcons.h"
#include "ui/UiTrace.h"
#include "ui/DisplayPreferences.h"

#include <cstdlib>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <vector>
#include <string>
#include <fstream>
#include <ArduinoJson.h>

#if defined(ARDUINO) && defined(ESP_PLATFORM)
#include <esp_bt.h>
#include <esp_heap_caps.h>
#include <esp_ota_ops.h>
#endif

#if defined(SDL_h_)
static LGFX lcd(320, 480, 2);
#elif defined(ARDUINO) && defined(ESP_PLATFORM)
#if defined(COLOR_KIT_PRO)
#include <ColorKitPro.h>
static ColorKitPro lcd;
#else
#include <ColorKitGrande.h>
static ColorKitGrande lcd;
#endif
#elif defined(ESP_PLATFORM) && !defined(ARDUINO)
#include <esp_timer.h>
static LGFX lcd;
#else
static LGFX lcd;
#endif

static lv_display_t *disp;
// Keep native smooth while reducing ESP32 DRAM pressure.
static uint8_t *draw_buf = nullptr;
static size_t draw_buf_size = 0;
static lv_obj_t *home_screen = nullptr;
static bool restore_route_pending = true;
static bool update_dialog_pending = false;
static lv_obj_t *update_dialog = nullptr;
static lv_obj_t *update_dialog_status = nullptr;
static lv_obj_t *update_dialog_buttons = nullptr;
static lv_timer_t *update_dialog_timer = nullptr;

static void launch_route(const std::string &route);

static void update_service_event(const SystemEvent &, void *) { update_dialog_pending = true; }

static void close_update_dialog()
{
  if (update_dialog_timer) { lv_timer_delete(update_dialog_timer); update_dialog_timer=nullptr; }
  if (update_dialog && lv_obj_is_valid(update_dialog)) lv_obj_delete(update_dialog);
  update_dialog=nullptr; update_dialog_status=nullptr;
  update_dialog_buttons=nullptr;
}

static void update_dialog_timer_cb(lv_timer_t *)
{
  UpdateService &updates=UpdateService::instance();
  if (!update_dialog_status || !lv_obj_is_valid(update_dialog_status)) return;
  char text[96];
  if (updates.state()==UpdateService::State::Downloading)
    std::snprintf(text,sizeof(text),"Downloading and verifying... %d%%",updates.progress_percent());
  else if (updates.state()==UpdateService::State::Error)
  {
    std::snprintf(text,sizeof(text),"Update failed: %s",updates.error().c_str());
    if (update_dialog_buttons && lv_obj_is_valid(update_dialog_buttons)) lv_obj_clear_flag(update_dialog_buttons,LV_OBJ_FLAG_HIDDEN);
  }
  else if (updates.state()==UpdateService::State::ReadyToReboot)
    std::snprintf(text,sizeof(text),"Verified. Restarting...");
  else return;
  lv_label_set_text(update_dialog_status,text);
}

static void update_dialog_action(lv_event_t *event)
{
  const intptr_t action=(intptr_t)lv_event_get_user_data(event);
  UpdateService &updates=UpdateService::instance();
  if (action==1) {
    if (updates.install()) {
      lv_label_set_text(update_dialog_status,"Downloading and verifying... 0%");
      update_dialog_timer=lv_timer_create(update_dialog_timer_cb,150,nullptr);
      lv_obj_t *button=(lv_obj_t*)lv_event_get_target(event);
      lv_obj_t *row=lv_obj_get_parent(button);
      lv_obj_add_flag(row,LV_OBJ_FLAG_HIDDEN);
    }
  } else {
    if (action==2) updates.ignore_available(); else updates.dismiss();
    close_update_dialog();
  }
}

static lv_obj_t *update_dialog_button(lv_obj_t *parent, const char *text, intptr_t action)
{
  lv_obj_t *button=lv_button_create(parent); lv_obj_set_height(button,40);
  lv_obj_add_event_cb(button,update_dialog_action,LV_EVENT_CLICKED,(void*)action);
  lv_obj_t *label=lv_label_create(button); lv_label_set_text(label,text); lv_obj_center(label); return button;
}

static void show_update_dialog()
{
  if (update_dialog && lv_obj_is_valid(update_dialog)) return;
  UpdateService &updates=UpdateService::instance();
  update_dialog=lv_obj_create(lv_layer_top()); lv_obj_set_size(update_dialog,300,300); lv_obj_center(update_dialog);
  lv_obj_set_style_bg_color(update_dialog,lv_color_hex(0x20243a),0);
  lv_obj_set_style_border_color(update_dialog,lv_color_hex(0x64c8ff),0);
  lv_obj_set_style_border_width(update_dialog,2,0); lv_obj_set_style_pad_all(update_dialog,14,0);
  lv_obj_set_layout(update_dialog,LV_LAYOUT_FLEX); lv_obj_set_flex_flow(update_dialog,LV_FLEX_FLOW_COLUMN);
  lv_obj_clear_flag(update_dialog,LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_t *title=lv_label_create(update_dialog); lv_label_set_text(title,"Gadget OS update available");
  lv_obj_set_style_text_font(title,&lv_font_montserrat_24,0); lv_obj_set_style_text_color(title,lv_color_white(),0);
  lv_obj_t *versions=lv_label_create(update_dialog); char version_text[96];
  std::snprintf(version_text,sizeof(version_text),"Installed: %s\nAvailable: %s",updates.current_version(),updates.available_version().c_str());
  lv_label_set_text(versions,version_text); lv_obj_set_style_text_color(versions,lv_color_hex(0xb7bdd7),0);
  lv_obj_t *notes=lv_label_create(update_dialog); lv_obj_set_width(notes,lv_pct(100));
  lv_label_set_long_mode(notes,LV_LABEL_LONG_WRAP); lv_label_set_text(notes,updates.release_notes().empty()?"A new version is ready to install.":updates.release_notes().c_str());
  lv_obj_set_flex_grow(notes,1); lv_obj_set_style_text_color(notes,lv_color_white(),0);
  update_dialog_status=lv_label_create(update_dialog); lv_label_set_text(update_dialog_status,"");
  lv_obj_t *buttons=lv_obj_create(update_dialog); lv_obj_set_size(buttons,lv_pct(100),50);
  update_dialog_buttons=buttons;
  lv_obj_set_style_bg_opa(buttons,LV_OPA_TRANSP,0); lv_obj_set_style_border_width(buttons,0,0); lv_obj_set_style_pad_all(buttons,0,0);
  lv_obj_set_layout(buttons,LV_LAYOUT_FLEX); lv_obj_set_flex_flow(buttons,LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(buttons,LV_FLEX_ALIGN_SPACE_BETWEEN,LV_FLEX_ALIGN_CENTER,LV_FLEX_ALIGN_CENTER);
  update_dialog_button(buttons,"Install",1); update_dialog_button(buttons,"Ignore",2); update_dialog_button(buttons,"Close",3);
}

#if defined(SDL_h_)
static uint32_t native_capture_started_ms = 0;
static bool native_touch_enabled = false;
static bool native_touch_pressed = false;
static lv_coord_t native_touch_x = 0;
static lv_coord_t native_touch_y = 0;
static FILE *native_video_pipe = nullptr;
static uint32_t native_video_started_ms = 0;
static uint32_t native_video_next_frame = 0;
static void native_push_touch(lv_coord_t x, lv_coord_t y, bool pressed);

struct NativeTutorialAction {
  std::string type, text, route;
  uint32_t duration = 700;
  int x = 0, y = 0, x2 = 0, y2 = 0, index = 0;
};
static std::vector<NativeTutorialAction> native_tutorial_actions;
static size_t native_tutorial_index = 0;
static uint32_t native_tutorial_action_started = 0;
static bool native_tutorial_action_applied = false;
static lv_obj_t *native_tutorial_caption = nullptr;
static lv_obj_t *native_tutorial_touch_dot = nullptr;

static lv_obj_t *native_find_label(lv_obj_t *root, const std::string &needle)
{
  if (!root) return nullptr;
  if (lv_obj_check_type(root, &lv_label_class)) {
    const char *value = lv_label_get_text(root);
    if (value && std::string(value).find(needle) != std::string::npos &&
        !lv_obj_has_flag(root, LV_OBJ_FLAG_HIDDEN)) return root;
  }
  const uint32_t count = lv_obj_get_child_count(root);
  for (uint32_t i = 0; i < count; ++i) {
    if (lv_obj_t *found = native_find_label(lv_obj_get_child(root, i), needle)) return found;
  }
  return nullptr;
}

static void native_show_caption(const std::string &text)
{
  if (!native_tutorial_caption || !lv_obj_is_valid(native_tutorial_caption)) {
    native_tutorial_caption = lv_label_create(lv_layer_top());
    lv_obj_set_width(native_tutorial_caption, 300);
    lv_obj_set_style_bg_color(native_tutorial_caption, lv_color_hex(0x101426), 0);
    lv_obj_set_style_bg_opa(native_tutorial_caption, LV_OPA_90, 0);
    lv_obj_set_style_text_color(native_tutorial_caption, lv_color_white(), 0);
    lv_obj_set_style_pad_all(native_tutorial_caption, 7, 0);
    lv_obj_set_style_radius(native_tutorial_caption, 8, 0);
    lv_obj_align(native_tutorial_caption, LV_ALIGN_BOTTOM_MID, 0, -8);
  }
  lv_label_set_text(native_tutorial_caption, text.c_str());
  if (text.empty()) lv_obj_add_flag(native_tutorial_caption, LV_OBJ_FLAG_HIDDEN);
  else lv_obj_clear_flag(native_tutorial_caption, LV_OBJ_FLAG_HIDDEN);
}

static void native_show_touch(int x, int y, bool pressed)
{
  if (!native_tutorial_touch_dot || !lv_obj_is_valid(native_tutorial_touch_dot)) {
    native_tutorial_touch_dot = lv_obj_create(lv_layer_top());
    lv_obj_set_size(native_tutorial_touch_dot, 22, 22);
    lv_obj_set_style_radius(native_tutorial_touch_dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(native_tutorial_touch_dot, lv_color_hex(0xffd43b), 0);
    lv_obj_set_style_bg_opa(native_tutorial_touch_dot, LV_OPA_70, 0);
    lv_obj_set_style_border_color(native_tutorial_touch_dot, lv_color_white(), 0);
    lv_obj_set_style_border_width(native_tutorial_touch_dot, 2, 0);
    lv_obj_clear_flag(native_tutorial_touch_dot, LV_OBJ_FLAG_SCROLLABLE);
  }
  lv_obj_set_pos(native_tutorial_touch_dot, x - 11, y - 11);
  if (pressed) lv_obj_clear_flag(native_tutorial_touch_dot, LV_OBJ_FLAG_HIDDEN);
  else lv_obj_add_flag(native_tutorial_touch_dot, LV_OBJ_FLAG_HIDDEN);
}

static void native_tap_object(lv_obj_t *obj)
{
  if (!obj) return;
  if (lv_obj_check_type(obj, &lv_label_class) && lv_obj_get_parent(obj)) {
    obj = lv_obj_get_parent(obj);
    // Launcher captions live beside their icon button inside a tile. Use the
    // button when present; ordinary settings/result/calculator labels already
    // have their actionable row/button as the direct parent.
    for (uint32_t i=0; i<lv_obj_get_child_count(obj); ++i) {
      lv_obj_t *child=lv_obj_get_child(obj,i);
      if (lv_obj_check_type(child, &lv_button_class)) { obj=child; break; }
    }
  }
  lv_area_t area{}; lv_obj_get_coords(obj, &area);
  const int x = (area.x1 + area.x2) / 2, y = (area.y1 + area.y2) / 2;
  native_push_touch(x, y, true); native_show_touch(x, y, true);
  lv_obj_send_event(obj, LV_EVENT_CLICKED, nullptr);
  // The semantic event is delivered synchronously. Release immediately so a
  // newly loaded screen cannot inherit the press and receive a ghost click;
  // the yellow indicator remains visible for the action's display duration.
  native_push_touch(x, y, false);
}

static lv_obj_t *native_find_type(lv_obj_t *root, const lv_obj_class_t *klass, int &wanted)
{
  if (!root) return nullptr;
  if (lv_obj_check_type(root, klass) && !lv_obj_has_flag(root, LV_OBJ_FLAG_HIDDEN)) {
    if (wanted-- == 0) return root;
  }
  for (uint32_t i = 0; i < lv_obj_get_child_count(root); ++i)
    if (lv_obj_t *found = native_find_type(lv_obj_get_child(root, i), klass, wanted)) return found;
  return nullptr;
}

static void native_load_tutorial()
{
  const char *path = std::getenv("GADGET_OS_TUTORIAL_PATH");
  if (!path || !path[0]) return;
  std::ifstream input(path);
  JsonDocument doc;
  if (!input || deserializeJson(doc, input) != DeserializationError::Ok) {
    std::fprintf(stderr, "[UI][Tutorial] cannot read %s\n", path); std::exit(4);
  }
  for (JsonObject item : doc["actions"].as<JsonArray>()) {
    NativeTutorialAction action;
    action.type = item["type"] | "wait"; action.text = item["text"] | "";
    action.route = item["route"] | ""; action.duration = item["duration_ms"] | 700;
    action.x = item["x"] | 0; action.y = item["y"] | 0;
    action.x2 = item["x2"] | action.x; action.y2 = item["y2"] | action.y;
    action.index = item["index"] | 0; native_tutorial_actions.push_back(action);
  }
  native_tutorial_action_started = SDL_GetTicks() + 1200;
}

static void native_run_tutorial()
{
  if (native_tutorial_actions.empty() || native_tutorial_index >= native_tutorial_actions.size()) return;
  const uint32_t now = SDL_GetTicks();
  if (now < native_tutorial_action_started) return;
  NativeTutorialAction &a = native_tutorial_actions[native_tutorial_index];
  const uint32_t elapsed = now - native_tutorial_action_started;
  if (!native_tutorial_action_applied) {
    native_tutorial_action_applied = true;
    if (a.type == "caption") native_show_caption(a.text);
    else if (a.type == "route") launch_route(a.route);
    else if (a.type == "tap_text") native_tap_object(native_find_label(lv_screen_active(), a.text));
    else if (a.type == "focus_textarea") {
      int n=a.index; lv_obj_t *ta=native_find_type(lv_screen_active(), &lv_textarea_class, n);
      if (ta) { lv_obj_add_state(ta, LV_STATE_FOCUSED); lv_obj_send_event(ta, LV_EVENT_FOCUSED, nullptr); }
    }
    else if (a.type == "tap") { native_push_touch(a.x,a.y,true); native_show_touch(a.x,a.y,true); }
    else if (a.type == "type") {
      int n=0; lv_obj_t *kb=native_find_type(lv_screen_active(), &lv_keyboard_class, n);
      lv_obj_t *ta=kb ? lv_keyboard_get_textarea(kb) : nullptr;
      if (ta) lv_textarea_set_text(ta, "");
    } else if (a.type == "roller") {
      int n=a.index; lv_obj_t *roller=native_find_type(lv_screen_active(), &lv_roller_class, n);
      if (roller) { lv_roller_set_selected(roller, (uint16_t)a.x, LV_ANIM_ON); lv_obj_send_event(roller, LV_EVENT_VALUE_CHANGED, nullptr); }
    } else if (a.type == "toggle") {
      int n=a.index; lv_obj_t *sw=native_find_type(lv_screen_active(), &lv_switch_class, n);
      if (sw) { if (a.x) lv_obj_add_state(sw, LV_STATE_CHECKED); else lv_obj_clear_state(sw, LV_STATE_CHECKED); lv_obj_send_event(sw, LV_EVENT_VALUE_CHANGED, nullptr); }
    } else if (a.type == "weather_fixture") {
      std::vector<WeatherLocation> locations = {
        {"Zurich", "Canton of Zurich", "Switzerland", "Europe/Zurich", 47.366673, 8.550000},
        {"New York", "New York", "United States", "America/New_York", 40.714272, -74.005966}
      };
      WeatherLocationStore::save(locations, 0);
      WeatherService::instance().reload_from_config();
      launch_route("settings_weather");
    }
  }
  if (a.type == "type") {
    int n=0; lv_obj_t *kb=native_find_type(lv_screen_active(), &lv_keyboard_class, n);
    lv_obj_t *ta=kb ? lv_keyboard_get_textarea(kb) : nullptr;
    size_t chars = a.text.empty() ? 0 : std::min(a.text.size(), (size_t)(elapsed * a.text.size() / std::max(1U, a.duration)));
    if (ta) { std::string part=a.text.substr(0, chars); lv_textarea_set_text(ta, part.c_str()); }
  } else if (a.type == "swipe") {
    const float p=std::min(1.0f, elapsed/(float)std::max(1U,a.duration));
    int x=a.x+(int)((a.x2-a.x)*p), y=a.y+(int)((a.y2-a.y)*p);
    native_push_touch(x,y,true); native_show_touch(x,y,true);
  }
  if (elapsed >= a.duration) {
    native_push_touch(a.x2,a.y2,false); native_show_touch(a.x2,a.y2,false);
    ++native_tutorial_index; native_tutorial_action_started=now; native_tutorial_action_applied=false;
  }
}

static void native_record_video_if_requested()
{
  const char *path = std::getenv("GADGET_OS_VIDEO_PATH");
  if (!path || !path[0]) return;
  if (!native_video_pipe) {
    char command[1024] = {0};
    std::snprintf(command, sizeof(command),
                  "ffmpeg -hide_banner -loglevel error -y -f rawvideo -pixel_format rgb24 "
                  "-video_size 320x480 -framerate 30 -i - -vf scale=640:960:flags=neighbor "
                  "-c:v libx264 -pix_fmt yuv420p -movflags +faststart '%s'", path);
    native_video_pipe = popen(command, "w");
    if (!native_video_pipe) std::exit(3);
    native_video_started_ms = SDL_GetTicks();
    native_video_next_frame = 0;
  }
  const uint32_t elapsed = SDL_GetTicks() - native_video_started_ms;
  std::vector<uint8_t> rgb(320U * 480U * 3U);
  while (native_video_next_frame * 1000U / 30U <= elapsed) {
    lv_refr_now(disp);
    lcd.readRectRGB(0, 0, 320, 480, rgb.data());
    if (std::fwrite(rgb.data(), 1, rgb.size(), native_video_pipe) != rgb.size()) std::exit(3);
    ++native_video_next_frame;
  }
  unsigned long duration_ms = 0;
  if (const char *duration = std::getenv("GADGET_OS_VIDEO_DURATION_MS"))
    duration_ms = std::strtoul(duration, nullptr, 10);
  if (duration_ms && elapsed >= duration_ms) {
    pclose(native_video_pipe);
    native_video_pipe = nullptr;
    std::exit(0);
  }
}

static void native_capture_if_requested()
{
  const char *path = std::getenv("GADGET_OS_SCREENSHOT_PATH");
  if (!path || !path[0]) return;

  if (native_capture_started_ms == 0) {
    native_capture_started_ms = SDL_GetTicks();
    return;
  }

  unsigned long delay_ms = 800;
  if (const char *configured_delay = std::getenv("GADGET_OS_SCREENSHOT_DELAY_MS"))
    delay_ms = std::strtoul(configured_delay, nullptr, 10);
  if (SDL_GetTicks() - native_capture_started_ms < delay_ms) return;

  lv_refr_now(disp);
  const int width = lcd.width();
  const int height = lcd.height();
  std::vector<uint8_t> rgb(static_cast<size_t>(width) * static_cast<size_t>(height) * 3U);
  lcd.readRectRGB(0, 0, width, height, rgb.data());

  FILE *output = std::fopen(path, "wb");
  if (!output) {
    std::fprintf(stderr, "[UI][Docs] cannot write screenshot: %s\n", path);
    std::exit(2);
  }
  std::fprintf(output, "P6\n%d %d\n255\n", width, height);
  const size_t written = std::fwrite(rgb.data(), 1, rgb.size(), output);
  std::fclose(output);
  if (written != rgb.size()) {
    std::fprintf(stderr, "[UI][Docs] incomplete screenshot: %s\n", path);
    std::exit(2);
  }

  std::fprintf(stdout, "[UI][Docs] screenshot written: %s\n", path);
  std::exit(0);
}

static void native_push_touch(lv_coord_t x, lv_coord_t y, bool pressed)
{
  native_touch_enabled = true;
  native_touch_x = x;
  native_touch_y = y;
  native_touch_pressed = pressed;
}
#endif

static uint32_t splash_now_ms()
{
#if defined(ARDUINO)
  return (uint32_t)millis();
#elif defined(SDL_h_)
  return SDL_GetTicks();
#elif defined(ESP_PLATFORM)
  return (uint32_t)(esp_timer_get_time() / 1000ULL);
#else
  return 0;
#endif
}

static void show_splash_screen(uint32_t duration_ms)
{
  lv_obj_t *splash = lv_obj_create(nullptr);
  lv_obj_set_style_bg_color(splash, lv_color_hex(0x1e2030), 0);
  lv_obj_set_style_bg_opa(splash, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(splash, 0, 0);
  lv_obj_set_style_pad_all(splash, 0, 0);
  lv_obj_clear_flag(splash, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t *title = lv_label_create(splash);
  lv_label_set_text(title, "Gadget OS by");
  lv_obj_set_style_text_color(title, lv_color_white(), 0);
  lv_obj_align(title, LV_ALIGN_CENTER, 0, -76);

  lv_obj_t *logo = lv_image_create(splash);
  lv_image_set_src(logo, &thingpulse_logo);
  lv_obj_align(logo, LV_ALIGN_CENTER, 0, 18);

  lv_screen_load(splash);
  lv_refr_now(nullptr);

  const uint32_t started_ms = splash_now_ms();
  while ((uint32_t)(splash_now_ms() - started_ms) < duration_ms) {
    lv_timer_handler();
#if defined(SDL_h_)
    SDL_Delay(5);
#elif defined(ARDUINO)
    delay(5);
#endif
  }

  lv_obj_t *home = lv_obj_create(nullptr);
  lv_screen_load(home);
  lv_obj_delete(splash);
}

struct FlushPerfStats {
  uint32_t frame_start_ms = 0;
  uint32_t frame_flushes = 0;
  uint32_t frame_pixels = 0;
  uint32_t frame_io_ms = 0;

  uint32_t sample_frames = 0;
  uint32_t total_frame_ms = 0;
  uint32_t total_flushes = 0;
  uint32_t total_pixels = 0;
  uint32_t total_io_ms = 0;
  uint32_t min_frame_ms = UINT32_MAX;
  uint32_t max_frame_ms = 0;
};

static bool flush_perf_enabled = false;
static FlushPerfStats flush_perf;

static uint32_t flush_perf_now_ms()
{
#if defined(ARDUINO)
  return (uint32_t)millis();
#elif defined(ESP_PLATFORM)
  return (uint32_t)(esp_timer_get_time() / 1000ULL);
#else
  return SDL_GetTicks();
#endif
}

static void flush_perf_reset()
{
  flush_perf = FlushPerfStats{};
}

static void flush_perf_finish_frame(uint32_t finished_ms)
{
  if (!flush_perf_enabled || flush_perf.frame_flushes == 0) return;

  const uint32_t frame_ms = finished_ms - flush_perf.frame_start_ms;
  flush_perf.sample_frames++;
  flush_perf.total_frame_ms += frame_ms;
  flush_perf.total_flushes += flush_perf.frame_flushes;
  flush_perf.total_pixels += flush_perf.frame_pixels;
  flush_perf.total_io_ms += flush_perf.frame_io_ms;
  if (frame_ms < flush_perf.min_frame_ms) flush_perf.min_frame_ms = frame_ms;
  if (frame_ms > flush_perf.max_frame_ms) flush_perf.max_frame_ms = frame_ms;

  flush_perf.frame_start_ms = 0;
  flush_perf.frame_flushes = 0;
  flush_perf.frame_pixels = 0;
  flush_perf.frame_io_ms = 0;

  constexpr uint32_t report_frames = 20;
  if (flush_perf.sample_frames >= report_frames) {
    UI_TRACE("Perf",
             "frames=%u frame_ms avg=%u min=%u max=%u flushes_avg=%u pixels_avg=%u io_ms_avg=%u",
             (unsigned)flush_perf.sample_frames,
             (unsigned)(flush_perf.total_frame_ms / flush_perf.sample_frames),
             (unsigned)flush_perf.min_frame_ms,
             (unsigned)flush_perf.max_frame_ms,
             (unsigned)(flush_perf.total_flushes / flush_perf.sample_frames),
             (unsigned)(flush_perf.total_pixels / flush_perf.sample_frames),
             (unsigned)(flush_perf.total_io_ms / flush_perf.sample_frames));
    flush_perf_reset();
  }
}

void apply_display_preferences(void)
{
  ConfigStoreService &cfg = ConfigStoreService::instance();
  int rotation = 0;
  int brightness_percent = 100;
  bool show_fps = false;
  if (cfg.load("display")) {
    rotation = cfg.get_int("rotation", 0);
    brightness_percent = cfg.get_int("brightness_percent", 100);
    show_fps = cfg.get_bool("show_fps", false);
  }

  if (flush_perf_enabled != show_fps) {
    flush_perf_enabled = show_fps;
    flush_perf_reset();
  }

  if (rotation < 0 || rotation > 3) rotation = 0;
  if (brightness_percent < 5) brightness_percent = 5;
  if (brightness_percent > 100) brightness_percent = 100;

  lcd.setRotation(rotation);

  if (disp) {
    lv_display_set_resolution(disp, lcd.width(), lcd.height());
#if LV_USE_PERF_MONITOR
    if (show_fps)
      lv_sysmon_show_performance(disp);
    else
      lv_sysmon_hide_performance(disp);
#endif
    lv_obj_invalidate(lv_screen_active());
    lv_refr_now(disp);
  }

#if defined(ARDUINO) && defined(ESP_PLATFORM)
  const uint8_t level = (uint8_t)((brightness_percent * 255) / 100);
  lcd.setBrightness(level);
#endif
}

static void launch_route(const std::string &route)
{
  UI_TRACE("Route", "launch_route route=%s", route.c_str());

  if (route == "home") {
    UI_TRACE("Route", "loading home screen");
    if (home_screen && lv_obj_is_valid(home_screen))
      lv_screen_load(home_screen);
    UiSessionState::set_route("home");
    return;
  }

  if (route == "settings") {
    UI_TRACE("Route", "launch settings");
    settings_launch();
    return;
  }

  if (route == "settings_system" || route == "settings_wifi" || route == "settings_time" || route == "settings_weather" || route == "settings_planes" || route == "settings_display") {
    UI_TRACE("Route", "launch settings subroute=%s", route.c_str());
    settings_launch_with_route(route.c_str());
    return;
  }

  if (route == "weather") {
    UI_TRACE("Route", "launch weather");
    weather_app_launch();
    return;
  }

  if (route == "weather_icon_test") {
    UI_TRACE("Route", "launch weather_icon_test");
    weather_icon_test_app_launch();
    return;
  }

  if (route == "planes") {
    UI_TRACE("Route", "launch planes");
    planes_app_launch();
    return;
  }

  if (route == "calculator") {
    UI_TRACE("Route", "launch calculator");
    calculator_app_launch();
    return;
  }

  UI_TRACE("Route", "unknown route=%s fallback=home", route.c_str());
  UiSessionState::set_route("home");
}

#if defined(SDL_h_) || (defined(ARDUINO) && defined(ESP_PLATFORM))
static void touch_read(lv_indev_t *indev, lv_indev_data_t *data)
{
  (void)indev;
  static lv_coord_t last_x = 0;
  static lv_coord_t last_y = 0;
  lgfx::touch_point_t tp{};
  bool pressed;
#if defined(SDL_h_)
  if (native_touch_enabled) {
    tp.x = native_touch_x;
    tp.y = native_touch_y;
    pressed = native_touch_pressed;
  } else
#endif
  {
    pressed = lcd.getTouch(&tp);
  }

  if (pressed) {
    last_x = tp.x;
    last_y = tp.y;
  }

  // Keep last valid coordinates when released to avoid feeding garbage into LVGL.
  data->point.x = last_x;
  data->point.y = last_y;
  data->state = pressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
}
#endif

static void app_icon_clicked(lv_event_t *e)
{
  if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    const App *app = &AppRegistry::instance().apps()[idx];
    UI_TRACE("Tap", "app_icon_clicked idx=%d app=%s", idx, app && app->name ? app->name : "<null>");
    if (app->on_launch) app->on_launch();
  }
}

static void create_app_tile(lv_obj_t *grid, const App &app, int app_index)
{
  lv_obj_t *tile = lv_obj_create(grid);
  lv_obj_set_size(tile, 88, 112);
  lv_obj_clear_flag(tile, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_opa(tile, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(tile, 0, 0);
  lv_obj_set_style_pad_all(tile, 2, 0);

  lv_obj_t *icon_btn = lv_button_create(tile);
  lv_obj_set_size(icon_btn, 68, 68);
  lv_obj_align(icon_btn, LV_ALIGN_TOP_MID, 0, 0);
  lv_obj_set_style_radius(icon_btn, 18, 0);
  lv_obj_set_style_bg_color(icon_btn, lv_color_hex(app.button_color), 0);
  lv_obj_set_style_border_width(icon_btn, 0, 0);
  lv_obj_set_style_shadow_width(icon_btn, 8, 0);
  lv_obj_set_style_shadow_color(icon_btn, lv_color_hex(0x000000), 0);
  lv_obj_set_style_shadow_opa(icon_btn, LV_OPA_20, 0);
  lv_obj_add_event_cb(icon_btn, app_icon_clicked, LV_EVENT_CLICKED,
                      (void *)(intptr_t)app_index);

  if (app.icon_create) {
    lv_obj_t *custom_icon = app.icon_create(icon_btn, 40);
    if (custom_icon) {
      // Decorative icon children must not consume the button's pointer events.
      lv_obj_clear_flag(custom_icon, LV_OBJ_FLAG_CLICKABLE);
      lv_obj_center(custom_icon);
    }
  } else {
    lv_obj_t *icon_label = lv_label_create(icon_btn);
    lv_label_set_text(icon_label, app.symbol ? app.symbol : "");
    lv_obj_set_style_text_font(icon_label, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(icon_label, lv_color_white(), 0);
    lv_obj_center(icon_label);
  }

  lv_obj_t *name_label = lv_label_create(tile);
  lv_label_set_text(name_label, app.name);
  lv_obj_set_style_text_color(name_label, lv_color_white(), 0);
  lv_obj_align(name_label, LV_ALIGN_BOTTOM_MID, 0, 0);
}

static lv_obj_t *create_app_page(lv_obj_t *pager, const char *title)
{
  lv_obj_t *page = lv_obj_create(pager);
  lv_obj_set_size(page, lv_pct(100), lv_pct(100));
  lv_obj_clear_flag(page, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_opa(page, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(page, 0, 0);
  lv_obj_set_style_radius(page, 0, 0);
  lv_obj_set_style_pad_all(page, 0, 0);

  lv_obj_t *grid = lv_obj_create(page);
  const bool has_title = title && title[0];
  lv_obj_set_pos(grid, 0, has_title ? 28 : 0);
  lv_obj_set_size(grid, lv_pct(100), has_title ? 392 : 420);
  lv_obj_clear_flag(grid, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_opa(grid, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(grid, 0, 0);
  lv_obj_set_style_pad_all(grid, 10, 0);
  lv_obj_set_style_pad_column(grid, 4, 0);
  lv_obj_set_style_pad_row(grid, 10, 0);
  lv_obj_set_layout(grid, LV_LAYOUT_FLEX);
  lv_obj_set_flex_flow(grid, LV_FLEX_FLOW_ROW_WRAP);
  lv_obj_set_flex_align(grid, LV_FLEX_ALIGN_SPACE_EVENLY,
                        LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

  if (has_title) {
    lv_obj_t *label = lv_label_create(page);
    lv_label_set_text(label, title);
    lv_obj_set_style_text_color(label, lv_color_hex(0xadb5bd), 0);
    lv_obj_align(label, LV_ALIGN_TOP_MID, 0, 5);
  }
  return grid;
}

static void home_pager_scrolled(lv_event_t *e)
{
  lv_obj_t *pager = (lv_obj_t *)lv_event_get_target(e);
  lv_obj_t *indicator = (lv_obj_t *)lv_event_get_user_data(e);
  if (!pager || !indicator || !lv_obj_is_valid(indicator)) return;
  const lv_coord_t page_width = lv_obj_get_width(pager);
  const bool labs_page = page_width > 0 && lv_obj_get_scroll_x(pager) >= page_width / 2;
  lv_label_set_text(indicator, labs_page ? "2 / 2" : "1 / 2");
}

static void create_home_screen(void)
{
  lv_obj_t *scr = lv_screen_active();
  home_screen = scr;
  lv_obj_set_style_pad_all(scr, 0, 0);
  lv_obj_set_style_border_width(scr, 0, 0);
  lv_obj_set_style_bg_color(scr, lv_color_hex(0x1e2030), 0);
  lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

  /* System bar */
  lv_obj_t *bar = lv_obj_create(scr);
  lv_obj_set_size(bar, lv_pct(100), 30);
  lv_obj_set_pos(bar, 0, 0);
  lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_color(bar, lv_color_hex(0x14162a), 0);
  lv_obj_set_style_border_width(bar, 0, 0);
  lv_obj_set_style_radius(bar, 0, 0);
  lv_obj_set_style_pad_all(bar, 0, 0);

  create_weather_center_label(bar, "esp32-gadget-os", 0);

  create_time_status_label(bar, 8);
  create_wifi_status_icon(bar, -8);

  /* Horizontally snapping launcher pages. */
  lv_obj_t *pager = lv_obj_create(scr);
  lv_obj_set_pos(pager, 0, 30);
  lv_obj_set_size(pager, lv_pct(100), 430);
  lv_obj_set_style_bg_opa(pager, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(pager, 0, 0);
  lv_obj_set_style_radius(pager, 0, 0);
  lv_obj_set_style_pad_all(pager, 0, 0);
  lv_obj_set_style_pad_column(pager, 0, 0);
  lv_obj_set_scroll_dir(pager, LV_DIR_HOR);
  lv_obj_set_scroll_snap_x(pager, LV_SCROLL_SNAP_CENTER);
  lv_obj_add_flag(pager, LV_OBJ_FLAG_SCROLL_ONE);
  lv_obj_set_scrollbar_mode(pager, LV_SCROLLBAR_MODE_OFF);
  lv_obj_set_layout(pager, LV_LAYOUT_FLEX);
  lv_obj_set_flex_flow(pager, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(pager, LV_FLEX_ALIGN_START,
                        LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

  lv_obj_t *stable_grid = create_app_page(pager, nullptr);
  const AppRegistry &reg = AppRegistry::instance();
  bool has_experimental_apps = false;
  for (int i = 0; i < reg.count(); ++i) {
    if (reg.apps()[i].availability == AppAvailability::Experimental) {
      has_experimental_apps = true;
      break;
    }
  }
  lv_obj_t *labs_grid = has_experimental_apps
                            ? create_app_page(pager, "Labs  -  swipe right to return")
                            : nullptr;

  if (has_experimental_apps) {
    lv_obj_t *indicator = lv_label_create(scr);
    lv_label_set_text(indicator, "1 / 2");
    lv_obj_set_style_text_color(indicator, lv_color_hex(0x868e96), 0);
    lv_obj_align(indicator, LV_ALIGN_BOTTOM_MID, 0, -2);
    lv_obj_add_event_cb(pager, home_pager_scrolled, LV_EVENT_SCROLL, indicator);
  }

  for (int i = 0; i < reg.count(); i++) {
    const App &app = reg.apps()[i];
    lv_obj_t *target_grid = app.availability == AppAvailability::Experimental
                                ? labs_grid
                                : stable_grid;
    if (target_grid) create_app_tile(target_grid, app, i);
  }
}

static void lvgl_flush(lv_display_t *d, const lv_area_t *area, uint8_t *px_map)
{
  const uint32_t flush_start_ms = flush_perf_enabled ? flush_perf_now_ms() : 0;
  int32_t w = lv_area_get_width(area);
  int32_t h = lv_area_get_height(area);
  const size_t px_count = (size_t)w * (size_t)h;
  const bool last_flush = lv_display_flush_is_last(d);
  if (flush_perf_enabled) {
    if (flush_perf.frame_flushes == 0) flush_perf.frame_start_ms = flush_start_ms;
    flush_perf.frame_flushes++;
    flush_perf.frame_pixels += (uint32_t)px_count;
  }
  lcd.startWrite();
  lcd.setAddrWindow(area->x1, area->y1, w, h);
  lcd.writePixels((lgfx::rgb565_t *)px_map, (int32_t)px_count);
  lcd.endWrite();
  if (flush_perf_enabled) {
    const uint32_t finished_ms = flush_perf_now_ms();
    flush_perf.frame_io_ms += finished_ms - flush_start_ms;
    if (last_flush) flush_perf_finish_frame(finished_ms);
  }
  lv_display_flush_ready(d);
}

#if defined(LV_TICK_CUSTOM) && LV_TICK_CUSTOM
static uint32_t get_tick_ms(void)
{
#if defined(ARDUINO)
  return millis();
#elif defined(ESP_PLATFORM)
  return (uint32_t)(esp_timer_get_time() / 1000ULL);
#else
  return SDL_GetTicks();
#endif
}
#endif

void setup(void)
{
  UI_TRACE("Boot", "setup start");
#if defined(ARDUINO) && defined(ESP_PLATFORM) && defined(GADGET_OS_BLE_ENABLED) && !GADGET_OS_BLE_ENABLED
  // BLE is not currently exposed by the Grande UI. Return the controller's
  // reserved DRAM to the general heap before Wi-Fi and TLS need it. Enabling
  // BLE later requires a reboot into a build with this flag set to 1.
  const esp_err_t bt_release = esp_bt_controller_mem_release(ESP_BT_MODE_BTDM);
  UI_TRACE("Boot", "BLE memory release result=%d", (int)bt_release);
#endif
  ServiceRegistry::instance().begin_all();

  lcd.init();
  apply_display_preferences();
  lv_init();

  disp = lv_display_create(lcd.width(), lcd.height());
  lv_display_set_color_format(disp, LV_COLOR_FORMAT_RGB565);
  lv_display_set_flush_cb(disp, lvgl_flush);

  size_t draw_buf_rows = 8U;
  const char *draw_buf_memory = "heap";
#if defined(ARDUINO) && defined(ESP_PLATFORM) && !defined(COLOR_KIT_PRO)
  // A single 32-row internal DMA buffer minimizes transaction overhead while
  // avoiding the ineffective RGB565-to-ILI9488 double-buffer pipeline.
  draw_buf_rows = 32U;
  draw_buf_size = static_cast<size_t>(lcd.width()) * draw_buf_rows * 2U;
  draw_buf = static_cast<uint8_t *>(heap_caps_malloc(
      draw_buf_size, MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA | MALLOC_CAP_8BIT));
  if (draw_buf) {
    draw_buf_memory = "internal-dma";
  } else {
    draw_buf = static_cast<uint8_t *>(heap_caps_malloc(
        draw_buf_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (draw_buf) {
      draw_buf_memory = "psram-single-fallback";
    } else {
      draw_buf = static_cast<uint8_t *>(heap_caps_malloc(draw_buf_size, MALLOC_CAP_8BIT));
      if (draw_buf) draw_buf_memory = "8bit-single-fallback";
    }
  }
#elif defined(ARDUINO) && defined(ESP_PLATFORM)
  draw_buf_size = static_cast<size_t>(lcd.width()) * draw_buf_rows * 2U;
  draw_buf = static_cast<uint8_t *>(heap_caps_malloc(draw_buf_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (draw_buf) draw_buf_memory = "psram";
  if (!draw_buf) {
    draw_buf = static_cast<uint8_t *>(heap_caps_malloc(draw_buf_size, MALLOC_CAP_8BIT));
    if (draw_buf) draw_buf_memory = "8bit-fallback";
  }
#else
  draw_buf_size = static_cast<size_t>(lcd.width()) * draw_buf_rows * 2U;
  draw_buf = static_cast<uint8_t *>(std::malloc(draw_buf_size));
#endif
  if (!draw_buf) {
    UI_TRACE("Boot", "draw buffer allocation failed bytes=%u", (unsigned)draw_buf_size);
    return;
  }
  UI_TRACE("Boot", "draw buffer rows=%u bytes=%u memory=%s",
           (unsigned)draw_buf_rows, (unsigned)draw_buf_size, draw_buf_memory);
#if defined(ARDUINO) && defined(ESP_PLATFORM) && !defined(COLOR_KIT_PRO)
  UI_TRACE("Boot", "internal RAM free=%u largest=%u",
           (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
           (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
#endif

  lv_display_set_buffers(disp, draw_buf, nullptr, draw_buf_size,
                         LV_DISPLAY_RENDER_MODE_PARTIAL);

  // The performance monitor is created by LVGL with the display. Apply the
  // persisted visibility preference now that the display and buffer exist.
  apply_display_preferences();

#if defined(LV_TICK_CUSTOM) && LV_TICK_CUSTOM
  lv_tick_set_cb(get_tick_ms);
#endif

#if defined(SDL_h_) || (defined(ARDUINO) && defined(ESP_PLATFORM))
  lv_indev_t *touch = lv_indev_create();
  lv_indev_set_type(touch, LV_INDEV_TYPE_POINTER);
  lv_indev_set_read_cb(touch, touch_read);
#endif

  UiSessionState::begin();
  EventBus::instance().subscribe(EventType::UpdateAvailable, update_service_event, nullptr);
#if defined(SDL_h_)
  if (const char *documentation_route = std::getenv("GADGET_OS_DOC_ROUTE"))
    UiSessionState::set_route(documentation_route);
#endif
  show_splash_screen(1000);
  create_home_screen();
#if defined(SDL_h_)
  native_load_tutorial();
#endif

  // Defer route restoration until first loop so home screen is fully ready
  restore_route_pending = true;
  UI_TRACE("Boot", "setup done restore_route_pending=1");
#if defined(GADGET_OS_OTA_ENABLED) && GADGET_OS_OTA_ENABLED
  // If rollback support is enabled in the bootloader, reaching a complete UI
  // and service startup is the point at which the new image is accepted.
  const esp_partition_t *running_partition=esp_ota_get_running_partition();
  esp_ota_img_states_t ota_state;
  if (running_partition && esp_ota_get_state_partition(running_partition,&ota_state)==ESP_OK &&
      ota_state==ESP_OTA_IMG_PENDING_VERIFY)
    esp_ota_mark_app_valid_cancel_rollback();
#endif
}

void loop(void)
{
  // Restore persisted route on first loop iteration after home screen is created
  if (restore_route_pending) {
    UI_TRACE("Boot", "restore_route_pending fired");
    restore_route_pending = false;
    const std::string route = UiSessionState::current_route();
    UI_TRACE("Boot", "current_route=%s", route.c_str());
    if (route != "home" && !route.empty()) {
      launch_route(route);
    }
  }

#if !defined(LV_TICK_CUSTOM) || !LV_TICK_CUSTOM
  static uint32_t prev_ms = 0;
#if defined(SDL_h_)
  uint32_t cur_ms = SDL_GetTicks();
#elif defined(ARDUINO)
  uint32_t cur_ms = (uint32_t)millis();
#elif defined(ESP_PLATFORM)
  uint32_t cur_ms = (uint32_t)(esp_timer_get_time() / 1000ULL);
#else
  uint32_t cur_ms = 0;
#endif
  lv_tick_inc(cur_ms - prev_ms);
  prev_ms = cur_ms;
#endif
  ServiceRegistry::instance().loop_all();
  if (update_dialog_pending) { update_dialog_pending=false; show_update_dialog(); }
#if defined(SDL_h_)
  native_run_tutorial();
#endif
  lv_task_handler();
#if defined(SDL_h_)
  native_capture_if_requested();
  native_record_video_if_requested();
#endif
}

#if defined(ESP_PLATFORM) && !defined(ARDUINO)
extern "C" {
int app_main(int, char**)
{
  setup();
  for (;;) {
    loop();
  }
  return 0;
}
}
#endif
