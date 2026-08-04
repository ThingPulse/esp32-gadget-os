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
#include "ServiceRegistry.h"
#include "apps/CalculatorApp.h"
#include "apps/PlanesApp.h"
#include "apps/SettingsApp.h"
#include "apps/WeatherApp.h"
#include "apps/WeatherIconTestApp.h"
#include "services/ConfigStoreService.h"
#include "services/TimeService.h"
#include "services/WeatherService.h"
#include "ui/UiSessionState.h"
#include "ui/StatusIcons.h"
#include "ui/UiTrace.h"
#include "ui/DisplayPreferences.h"

#include <cstdlib>
#include <cstdint>
#include <cstring>

#if defined(ARDUINO) && defined(ESP_PLATFORM)
#include <esp_heap_caps.h>
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

  if (route == "settings_wifi" || route == "settings_time" || route == "settings_weather" || route == "settings_planes" || route == "settings_display") {
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
  bool pressed = lcd.getTouch(&tp);

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

  /* App grid */
  lv_obj_t *grid = lv_obj_create(scr);
  lv_obj_set_pos(grid, 0, 30);
  lv_obj_set_size(grid, lv_pct(100), 450);
  lv_obj_set_style_bg_opa(grid, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(grid, 0, 0);
  lv_obj_set_style_pad_all(grid, 10, 0);
  lv_obj_set_style_pad_column(grid, 4, 0);
  lv_obj_set_style_pad_row(grid, 10, 0);
  lv_obj_set_layout(grid, LV_LAYOUT_FLEX);
  lv_obj_set_flex_flow(grid, LV_FLEX_FLOW_ROW_WRAP);
  lv_obj_set_flex_align(grid, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

  /* App tiles from registry */
  const AppRegistry &reg = AppRegistry::instance();
  for (int i = 0; i < reg.count(); i++) {
    const App &app = reg.apps()[i];

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
    lv_obj_add_event_cb(icon_btn, app_icon_clicked, LV_EVENT_CLICKED, (void*)(intptr_t)i);

    lv_obj_t *icon_label = lv_label_create(icon_btn);
    lv_label_set_text(icon_label, app.symbol);
    lv_obj_set_style_text_font(icon_label, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(icon_label, lv_color_white(), 0);
    lv_obj_center(icon_label);

    lv_obj_t *name_label = lv_label_create(tile);
    lv_label_set_text(name_label, app.name);
    lv_obj_set_style_text_color(name_label, lv_color_white(), 0);
    lv_obj_align(name_label, LV_ALIGN_BOTTOM_MID, 0, 0);
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
  create_home_screen();

  // Defer route restoration until first loop so home screen is fully ready
  restore_route_pending = true;
  UI_TRACE("Boot", "setup done restore_route_pending=1");
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
  lv_task_handler();
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
