#include "apps/BlueScanApp.h"

#include "AppRegistry.h"
#include "services/BlueScanService.h"
#include "ui/StatusIcons.h"
#include "ui/UiNav.h"
#include "ui/UiSessionState.h"

#include <cstdio>
#include <string>

APP_REGISTER_COLOR("BlueScan", LV_SYMBOL_BLUETOOTH, blue_scan_app_launch, 0x1971c2);

namespace {

constexpr int MAX_ROWS = BlueScanService::MAX_DEVICES;

struct BlueScanAppCtx {
  lv_obj_t *screen = nullptr;
  lv_obj_t *status_label = nullptr;
  lv_obj_t *rows[MAX_ROWS]{};
  lv_obj_t *primary_labels[MAX_ROWS]{};
  lv_obj_t *secondary_labels[MAX_ROWS]{};
  lv_timer_t *refresh_timer = nullptr;
  bool closing = false;
};

static BlueScanAppCtx app_ctx;

static void nav_back_event(lv_event_t *e)
{
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;

  app_ctx.closing = true;
  BlueScanService::instance().stop_scan();
  if (app_ctx.refresh_timer) {
    lv_timer_delete(app_ctx.refresh_timer);
    app_ctx.refresh_timer = nullptr;
  }

  UiSessionState::set_route("home");
  lv_obj_t *target_scr = (lv_obj_t *)lv_event_get_user_data(e);
  if (!target_scr || !lv_obj_is_valid(target_scr)) return;
  ui_load_screen(target_scr, true);
}

static void blue_scan_screen_delete_event(lv_event_t *e)
{
  (void)e;
  BlueScanService::instance().stop_scan();
  if (app_ctx.refresh_timer) {
    lv_timer_delete(app_ctx.refresh_timer);
    app_ctx.refresh_timer = nullptr;
  }
  app_ctx = BlueScanAppCtx{};
}

static void update_ui()
{
  if (app_ctx.closing) return;
  if (!app_ctx.screen || !lv_obj_is_valid(app_ctx.screen)) return;
  if (!app_ctx.status_label || !lv_obj_is_valid(app_ctx.status_label)) return;

  BlueScanService &svc = BlueScanService::instance();
  const BlueScanDevice *devices = svc.devices();

  lv_label_set_text(app_ctx.status_label, svc.status_text().c_str());

  for (int i = 0; i < MAX_ROWS; ++i) {
    if (!app_ctx.rows[i] || !lv_obj_is_valid(app_ctx.rows[i])) continue;
    if (!app_ctx.primary_labels[i] || !lv_obj_is_valid(app_ctx.primary_labels[i])) continue;
    if (!app_ctx.secondary_labels[i] || !lv_obj_is_valid(app_ctx.secondary_labels[i])) continue;

    if (!devices[i].valid) {
      lv_obj_add_flag(app_ctx.rows[i], LV_OBJ_FLAG_HIDDEN);
      continue;
    }

    lv_obj_clear_flag(app_ctx.rows[i], LV_OBJ_FLAG_HIDDEN);

    char primary_buf[96] = {0};
    std::snprintf(primary_buf, sizeof(primary_buf), "%s (%d dBm)", devices[i].name.c_str(), devices[i].rssi);
    lv_label_set_text(app_ctx.primary_labels[i], primary_buf);

    std::string secondary = devices[i].addr + " | " + devices[i].decoded;
    lv_label_set_text(app_ctx.secondary_labels[i], secondary.c_str());
  }
}

static void update_timer_cb(lv_timer_t *timer)
{
  (void)timer;
  update_ui();
}

}  // namespace

void blue_scan_app_launch(void)
{
  app_ctx.closing = false;
  BlueScanService::instance().start_scan();

  lv_obj_t *home_scr = lv_screen_active();

  lv_obj_t *scr = lv_obj_create(nullptr);
  lv_obj_set_style_bg_color(scr, lv_color_hex(0x1e2030), 0);
  lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
  lv_obj_set_style_pad_all(scr, 0, 0);
  lv_obj_set_style_border_width(scr, 0, 0);
  lv_obj_add_event_cb(scr, blue_scan_screen_delete_event, LV_EVENT_DELETE, nullptr);

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
  lv_label_set_text(title, "BlueScan");
  lv_obj_set_style_text_color(title, lv_color_white(), 0);
  lv_obj_align(title, LV_ALIGN_CENTER, 0, 0);

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

  app_ctx.status_label = lv_label_create(content);
  lv_obj_set_style_text_color(app_ctx.status_label, lv_color_hex(0xb7bdd7), 0);
  lv_label_set_text(app_ctx.status_label, "Starting scanner...");

  for (int i = 0; i < MAX_ROWS; ++i) {
    app_ctx.rows[i] = lv_obj_create(content);
    lv_obj_set_width(app_ctx.rows[i], lv_pct(100));
    lv_obj_set_style_bg_color(app_ctx.rows[i], lv_color_hex(0x1f2438), 0);
    lv_obj_set_style_bg_opa(app_ctx.rows[i], LV_OPA_70, 0);
    lv_obj_set_style_border_width(app_ctx.rows[i], 0, 0);
    lv_obj_set_style_radius(app_ctx.rows[i], 8, 0);
    lv_obj_set_style_pad_all(app_ctx.rows[i], 8, 0);
    lv_obj_set_layout(app_ctx.rows[i], LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(app_ctx.rows[i], LV_FLEX_FLOW_COLUMN);

    app_ctx.primary_labels[i] = lv_label_create(app_ctx.rows[i]);
    lv_obj_set_style_text_color(app_ctx.primary_labels[i], lv_color_white(), 0);

    app_ctx.secondary_labels[i] = lv_label_create(app_ctx.rows[i]);
    lv_obj_set_style_text_color(app_ctx.secondary_labels[i], lv_color_hex(0xb7bdd7), 0);
    lv_obj_set_style_text_font(app_ctx.secondary_labels[i], &lv_font_montserrat_14, 0);

    lv_obj_add_flag(app_ctx.rows[i], LV_OBJ_FLAG_HIDDEN);
  }

  app_ctx.screen = scr;
  app_ctx.refresh_timer = lv_timer_create(update_timer_cb, 1000, nullptr);

  update_ui();
  UiSessionState::set_route("blue_scan");
  ui_load_screen(scr, true);
}
