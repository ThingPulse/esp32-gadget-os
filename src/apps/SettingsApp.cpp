#include "apps/SettingsApp.h"
#include "AppRegistry.h"
#include <lvgl.h>

APP_REGISTER("Settings", LV_SYMBOL_SETTINGS, settings_launch);

// ── Shared helpers ────────────────────────────────────────────────────────

static void nav_to_target_screen(lv_event_t *e)
{
  if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
    lv_obj_t *target_scr = (lv_obj_t *)lv_event_get_user_data(e);
    lv_screen_load_anim(target_scr, LV_SCR_LOAD_ANIM_MOVE_RIGHT, 250, 0, true);
  }
}

static void make_top_bar(lv_obj_t *scr, const char *title, lv_obj_t *back_target)
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
  lv_obj_add_event_cb(back_btn, nav_to_target_screen, LV_EVENT_CLICKED, back_target);

  lv_obj_t *back_label = lv_label_create(back_btn);
  lv_label_set_text(back_label, LV_SYMBOL_LEFT);
  lv_obj_set_style_text_color(back_label, lv_color_white(), 0);
  lv_obj_center(back_label);

  lv_obj_t *lbl = lv_label_create(bar);
  lv_label_set_text(lbl, title);
  lv_obj_set_style_text_color(lbl, lv_color_white(), 0);
  lv_obj_align(lbl, LV_ALIGN_CENTER, 0, 0);
}

// ── WiFi screen ───────────────────────────────────────────────────────────

#define WIFI_KB_HEIGHT 200
#define WIFI_BAR_HEIGHT 44
#define WIFI_SCREEN_H 480

struct WifiCtx {
  lv_obj_t *content;
  lv_obj_t *kb;
};
static WifiCtx wifi_ctx;

static void wifi_ta_event(lv_event_t *e)
{
  lv_event_code_t code = lv_event_get_code(e);
  lv_obj_t *ta = (lv_obj_t *)lv_event_get_target(e);

  if (code == LV_EVENT_FOCUSED) {
    lv_obj_clear_flag(wifi_ctx.kb, LV_OBJ_FLAG_HIDDEN);
    lv_keyboard_set_textarea(wifi_ctx.kb, ta);
    lv_obj_set_height(wifi_ctx.content, WIFI_SCREEN_H - WIFI_BAR_HEIGHT - WIFI_KB_HEIGHT);
    lv_obj_scroll_to_view(ta, LV_ANIM_ON);
  } else if (code == LV_EVENT_DEFOCUSED) {
    lv_obj_add_flag(wifi_ctx.kb, LV_OBJ_FLAG_HIDDEN);
    lv_keyboard_set_textarea(wifi_ctx.kb, nullptr);
    lv_obj_set_height(wifi_ctx.content, WIFI_SCREEN_H - WIFI_BAR_HEIGHT);
  }
}

static void wifi_kb_event(lv_event_t *e)
{
  lv_event_code_t code = lv_event_get_code(e);
  if (code == LV_EVENT_READY || code == LV_EVENT_CANCEL) {
    lv_obj_t *ta = lv_keyboard_get_textarea(wifi_ctx.kb);
    if (ta) lv_obj_clear_state(ta, LV_STATE_FOCUSED);
    lv_keyboard_set_textarea(wifi_ctx.kb, nullptr);
    lv_obj_add_flag(wifi_ctx.kb, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_height(wifi_ctx.content, WIFI_SCREEN_H - WIFI_BAR_HEIGHT);
  }
}

static void open_wifi_screen(lv_event_t *e)
{
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;

  lv_obj_t *settings_scr = (lv_obj_t *)lv_event_get_user_data(e);

  lv_obj_t *scr = lv_obj_create(nullptr);
  lv_obj_set_style_bg_color(scr, lv_color_hex(0x1e2030), 0);
  lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
  lv_obj_set_style_pad_all(scr, 0, 0);
  lv_obj_set_style_border_width(scr, 0, 0);

  make_top_bar(scr, "WiFi", settings_scr);

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
  lv_textarea_set_password_mode(pass_input, true);
  lv_textarea_set_placeholder_text(pass_input, "Enter password");
  lv_obj_add_event_cb(pass_input, wifi_ta_event, LV_EVENT_FOCUSED, nullptr);
  lv_obj_add_event_cb(pass_input, wifi_ta_event, LV_EVENT_DEFOCUSED, nullptr);

  lv_obj_t *save_btn = lv_button_create(content);
  lv_obj_set_width(save_btn, lv_pct(100));
  lv_obj_set_height(save_btn, 44);
  lv_obj_add_event_cb(save_btn, nav_to_target_screen, LV_EVENT_CLICKED, settings_scr);

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

  lv_screen_load_anim(scr, LV_SCR_LOAD_ANIM_MOVE_LEFT, 250, 0, false);
}

void settings_launch(void)
{
  lv_obj_t *home_scr = lv_screen_active();

  lv_obj_t *scr = lv_obj_create(nullptr);
  lv_obj_set_style_bg_color(scr, lv_color_hex(0x1e2030), 0);
  lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
  lv_obj_set_style_pad_all(scr, 0, 0);
  lv_obj_set_style_border_width(scr, 0, 0);

  make_top_bar(scr, "Settings", home_scr);

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

  lv_screen_load_anim(scr, LV_SCR_LOAD_ANIM_MOVE_LEFT, 250, 0, false);
}
