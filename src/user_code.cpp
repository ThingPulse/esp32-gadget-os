#define LGFX_AUTODETECT
#include <LovyanGFX.h>
#include <LGFX_AUTODETECT.hpp>
#include <lvgl.h>
#include <cstring>

#if defined(SDL_h_)
static LGFX lcd(320, 480, 2);
#elif defined(ARDUINO) && defined(ESP_PLATFORM)
#include <ColorKitGrande.h>
static ColorKitGrande lcd;
#elif defined(ESP_PLATFORM) && !defined(ARDUINO)
#include <esp_timer.h>
static LGFX lcd;
#else
static LGFX lcd;
#endif

static lv_display_t *disp;
static uint8_t draw_buf[320 * 20 * 2];
static lv_obj_t *g_label;

#if defined(SDL_h_) || (defined(ARDUINO) && defined(ESP_PLATFORM))
static void touch_read(lv_indev_t *indev, lv_indev_data_t *data)
{
  lgfx::touch_point_t tp;
  bool pressed = lcd.getTouch(&tp);
  data->point.x = tp.x;
  data->point.y = tp.y;
  data->state = pressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
}
#endif

static void button_event_handler(lv_event_t *e)
{
  if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
    const char *current_text = lv_label_get_text(g_label);
    if (strcmp(current_text, "Hello") == 0) {
      lv_label_set_text(g_label, "You");
    } else {
      lv_label_set_text(g_label, "Hello");
    }
  }
}

static void lvgl_flush(lv_display_t *d, const lv_area_t *area, uint8_t *px_map)
{
  int32_t w = lv_area_get_width(area);
  int32_t h = lv_area_get_height(area);
  lcd.startWrite();
  lcd.setAddrWindow(area->x1, area->y1, w, h);
  lcd.writePixels((lgfx::rgb565_t *)px_map, w * h);
  lcd.endWrite();
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
  lcd.init();
  lv_init();

  disp = lv_display_create(lcd.width(), lcd.height());
  lv_display_set_color_format(disp, LV_COLOR_FORMAT_RGB565);
  lv_display_set_flush_cb(disp, lvgl_flush);
  lv_display_set_buffers(disp, draw_buf, nullptr, sizeof(draw_buf),
                         LV_DISPLAY_RENDER_MODE_PARTIAL);

#if defined(LV_TICK_CUSTOM) && LV_TICK_CUSTOM
  lv_tick_set_cb(get_tick_ms);
#endif

  lv_obj_t *btn = lv_button_create(lv_screen_active());
  lv_obj_center(btn);
  g_label = lv_label_create(btn);
  lv_label_set_text(g_label, "Hello");
  lv_obj_add_event_cb(btn, button_event_handler, LV_EVENT_CLICKED, nullptr);

#if defined(SDL_h_) || (defined(ARDUINO) && defined(ESP_PLATFORM))
  lv_indev_t *touch = lv_indev_create();
  lv_indev_set_type(touch, LV_INDEV_TYPE_POINTER);
  lv_indev_set_read_cb(touch, touch_read);
#endif
}

void loop(void)
{
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