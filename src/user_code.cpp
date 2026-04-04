#define LGFX_AUTODETECT
#include <LovyanGFX.h>
#include <LGFX_AUTODETECT.hpp>
#include <lvgl.h>
#include "AppRegistry.h"
#include "ServiceRegistry.h"

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

static void app_icon_clicked(lv_event_t *e)
{
  if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    const App *app = &AppRegistry::instance().apps()[idx];
    if (app->on_launch) app->on_launch();
  }
}

static void create_home_screen(void)
{
  lv_obj_t *scr = lv_screen_active();
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

  lv_obj_t *bar_label = lv_label_create(bar);
  lv_label_set_text(bar_label, "esp32-gadget-os");
  lv_obj_set_style_text_color(bar_label, lv_color_white(), 0);
  lv_obj_align(bar_label, LV_ALIGN_CENTER, 0, 0);

  /* App grid */
  lv_obj_t *grid = lv_obj_create(scr);
  lv_obj_set_pos(grid, 0, 30);
  lv_obj_set_size(grid, lv_pct(100), 450);
  lv_obj_set_style_bg_opa(grid, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(grid, 0, 0);
  lv_obj_set_style_pad_all(grid, 10, 0);
  lv_obj_set_layout(grid, LV_LAYOUT_FLEX);
  lv_obj_set_flex_flow(grid, LV_FLEX_FLOW_ROW_WRAP);
  lv_obj_set_flex_align(grid, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

  /* App tiles from registry */
  const AppRegistry &reg = AppRegistry::instance();
  for (int i = 0; i < reg.count(); i++) {
    const App &app = reg.apps()[i];

    lv_obj_t *tile = lv_obj_create(grid);
    lv_obj_set_size(tile, 96, 112);
    lv_obj_clear_flag(tile, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(tile, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(tile, 0, 0);
    lv_obj_set_style_pad_all(tile, 4, 0);

    lv_obj_t *icon_btn = lv_button_create(tile);
    lv_obj_set_size(icon_btn, 74, 74);
    lv_obj_align(icon_btn, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_radius(icon_btn, 18, 0);
    lv_obj_add_event_cb(icon_btn, app_icon_clicked, LV_EVENT_CLICKED, (void*)(intptr_t)i);

    lv_obj_t *icon_label = lv_label_create(icon_btn);
    lv_label_set_text(icon_label, app.symbol);
    lv_obj_set_style_text_font(icon_label, &lv_font_montserrat_24, 0);
    lv_obj_center(icon_label);

    lv_obj_t *name_label = lv_label_create(tile);
    lv_label_set_text(name_label, app.name);
    lv_obj_set_style_text_color(name_label, lv_color_white(), 0);
    lv_obj_align(name_label, LV_ALIGN_BOTTOM_MID, 0, 0);
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
  ServiceRegistry::instance().begin_all();

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

#if defined(SDL_h_) || (defined(ARDUINO) && defined(ESP_PLATFORM))
  lv_indev_t *touch = lv_indev_create();
  lv_indev_set_type(touch, LV_INDEV_TYPE_POINTER);
  lv_indev_set_read_cb(touch, touch_read);
#endif

  create_home_screen();
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