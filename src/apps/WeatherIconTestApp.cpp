#include "apps/WeatherIconTestApp.h"

#include "AppRegistry.h"
#include "ui/StatusIcons.h"
#include "ui/UiSessionState.h"
#include "ui/WeatherIconSet.h"

APP_REGISTER("Weather Icons", LV_SYMBOL_IMAGE, weather_icon_test_app_launch);

namespace {

struct IconSpec {
  WeatherIconKind kind;
  const char *label;
};

static void nav_back_event(lv_event_t *e)
{
  if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
    UiSessionState::set_route("home");
    lv_obj_t *target_scr = (lv_obj_t *)lv_event_get_user_data(e);
    if (!target_scr || !lv_obj_is_valid(target_scr)) return;
    lv_screen_load_anim(target_scr, LV_SCR_LOAD_ANIM_MOVE_RIGHT, 250, 0, true);
  }
}

}  // namespace

void weather_icon_test_app_launch(void)
{
  UiSessionState::set_route("weather_icon_test");
  lv_obj_t *home_scr = lv_screen_active();

  lv_obj_t *scr = lv_obj_create(nullptr);
  lv_obj_set_style_bg_color(scr, lv_color_hex(0x1e2030), 0);
  lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
  lv_obj_set_style_pad_all(scr, 0, 0);
  lv_obj_set_style_border_width(scr, 0, 0);

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
  lv_label_set_text(title, "Weather Icons");
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
  lv_obj_set_flex_flow(content, LV_FLEX_FLOW_ROW_WRAP);
  lv_obj_set_flex_align(content, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

  const IconSpec icons[] = {
      {WeatherIconKind::Clear, "Clear"},
      {WeatherIconKind::Cloudy, "Cloudy"},
      {WeatherIconKind::Rain, "Rain"},
      {WeatherIconKind::Snow, "Snow"},
      {WeatherIconKind::Storm, "Storm"},
      {WeatherIconKind::Fog, "Fog"},
  };

  for (const IconSpec &icon_spec : icons) {
    lv_obj_t *card = lv_obj_create(content);
    lv_obj_set_size(card, 140, 140);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(card, lv_color_hex(0x2a2d42), 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(card, 0, 0);
    lv_obj_set_style_radius(card, 10, 0);
    lv_obj_set_style_pad_all(card, 8, 0);
    lv_obj_set_layout(card, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(card, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t *label = lv_label_create(card);
    lv_label_set_text(label, icon_spec.label);
    lv_obj_set_style_text_color(label, lv_color_white(), 0);

    weather_icon_create(card, icon_spec.kind, 68);
  }

  lv_screen_load_anim(scr, LV_SCR_LOAD_ANIM_MOVE_LEFT, 250, 0, false);
}