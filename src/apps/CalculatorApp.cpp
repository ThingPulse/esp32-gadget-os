#include "apps/CalculatorApp.h"

#include "AppRegistry.h"
#include "ui/UiNav.h"
#include "ui/StatusIcons.h"
#include "ui/UiSessionState.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

APP_REGISTER_COLOR_RANK("Calculator", LV_SYMBOL_PLUS, calculator_app_launch, 0xf08c2e, 90);

namespace {

struct CalculatorCtx {
  lv_obj_t *display_label = nullptr;
  std::string input = "0";
  double accumulator = 0.0;
  char pending_op = 0;
  bool entering_new = true;
  bool has_error = false;
};

static CalculatorCtx calc_ctx;

static void nav_back_event(lv_event_t *e)
{
  if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
    UiSessionState::set_route("home");
    lv_obj_t *target_scr = (lv_obj_t *)lv_event_get_user_data(e);
    if (!target_scr || !lv_obj_is_valid(target_scr)) return;
    ui_load_screen(target_scr, true);
  }
}

static std::string format_value(double value)
{
  if (std::isnan(value) || std::isinf(value)) return "Error";

  char buf[64] = {0};
  std::snprintf(buf, sizeof(buf), "%.10g", value);
  return std::string(buf);
}

static void update_display()
{
  if (!calc_ctx.display_label || !lv_obj_is_valid(calc_ctx.display_label)) return;
  lv_label_set_text(calc_ctx.display_label, calc_ctx.input.c_str());
}

static double current_value()
{
  return std::strtod(calc_ctx.input.c_str(), nullptr);
}

static void set_error_state()
{
  calc_ctx.has_error = true;
  calc_ctx.input = "Error";
  calc_ctx.accumulator = 0.0;
  calc_ctx.pending_op = 0;
  calc_ctx.entering_new = true;
  update_display();
}

static void set_input_from_double(double value)
{
  calc_ctx.input = format_value(value);
  if (calc_ctx.input == "Error") {
    set_error_state();
    return;
  }
  update_display();
}

static bool apply_pending(double rhs)
{
  switch (calc_ctx.pending_op) {
    case '+':
      calc_ctx.accumulator += rhs;
      return true;
    case '-':
      calc_ctx.accumulator -= rhs;
      return true;
    case '*':
      calc_ctx.accumulator *= rhs;
      return true;
    case '/':
      if (rhs == 0.0) return false;
      calc_ctx.accumulator /= rhs;
      return true;
    default:
      calc_ctx.accumulator = rhs;
      return true;
  }
}

static void reset_calculator()
{
  calc_ctx.input = "0";
  calc_ctx.accumulator = 0.0;
  calc_ctx.pending_op = 0;
  calc_ctx.entering_new = true;
  calc_ctx.has_error = false;
  update_display();
}

static bool is_digit_key(const char *key)
{
  return key && key[0] >= '0' && key[0] <= '9' && key[1] == '\0';
}

static void handle_digit(const char *digit)
{
  if (calc_ctx.has_error) reset_calculator();

  if (calc_ctx.entering_new) {
    calc_ctx.input = digit;
    calc_ctx.entering_new = false;
  } else {
    if (calc_ctx.input == "0") calc_ctx.input = digit;
    else calc_ctx.input += digit;
  }
  update_display();
}

static void handle_decimal()
{
  if (calc_ctx.has_error) reset_calculator();

  if (calc_ctx.entering_new) {
    calc_ctx.input = "0.";
    calc_ctx.entering_new = false;
  } else if (calc_ctx.input.find('.') == std::string::npos) {
    calc_ctx.input += ".";
  }
  update_display();
}

static void handle_operator(char op)
{
  if (calc_ctx.has_error) return;

  double rhs = current_value();
  if (calc_ctx.pending_op == 0) {
    calc_ctx.accumulator = rhs;
  } else if (!calc_ctx.entering_new) {
    if (!apply_pending(rhs)) {
      set_error_state();
      return;
    }
  }

  calc_ctx.pending_op = op;
  calc_ctx.entering_new = true;
  set_input_from_double(calc_ctx.accumulator);
}

static void handle_equals()
{
  if (calc_ctx.has_error) return;
  if (calc_ctx.pending_op == 0) return;

  double rhs = current_value();
  if (!apply_pending(rhs)) {
    set_error_state();
    return;
  }

  calc_ctx.pending_op = 0;
  calc_ctx.entering_new = true;
  set_input_from_double(calc_ctx.accumulator);
}

static void handle_sign_toggle()
{
  if (calc_ctx.has_error) return;

  double value = current_value();
  value = -value;
  calc_ctx.input = format_value(value);
  calc_ctx.entering_new = false;
  update_display();
}

static void handle_percent()
{
  if (calc_ctx.has_error) return;

  double value = current_value() / 100.0;
  calc_ctx.input = format_value(value);
  calc_ctx.entering_new = false;
  update_display();
}

static void on_calc_button(lv_event_t *e)
{
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;

  const char *key = (const char *)lv_event_get_user_data(e);
  if (!key) return;

  if (is_digit_key(key)) {
    handle_digit(key);
    return;
  }

  if (std::strcmp(key, ".") == 0) {
    handle_decimal();
  } else if (std::strcmp(key, "AC") == 0) {
    reset_calculator();
  } else if (std::strcmp(key, "+/-") == 0) {
    handle_sign_toggle();
  } else if (std::strcmp(key, "%") == 0) {
    handle_percent();
  } else if (std::strcmp(key, "+") == 0) {
    handle_operator('+');
  } else if (std::strcmp(key, "-") == 0) {
    handle_operator('-');
  } else if (std::strcmp(key, "*") == 0) {
    handle_operator('*');
  } else if (std::strcmp(key, "/") == 0) {
    handle_operator('/');
  } else if (std::strcmp(key, "=") == 0) {
    handle_equals();
  }
}

static lv_obj_t *create_calc_btn(lv_obj_t *parent,
                                 const char *text,
                                 lv_coord_t x,
                                 lv_coord_t y,
                                 lv_coord_t w,
                                 lv_coord_t h,
                                 lv_color_t bg,
                                 lv_color_t fg)
{
  lv_obj_t *btn = lv_button_create(parent);
  lv_obj_set_pos(btn, x, y);
  lv_obj_set_size(btn, w, h);
  lv_obj_set_style_bg_color(btn, bg, 0);
  lv_obj_set_style_border_width(btn, 0, 0);
  lv_obj_set_style_radius(btn, h / 2, 0);

  lv_obj_t *label = lv_label_create(btn);
  lv_label_set_text(label, text);
  lv_obj_set_style_text_color(label, fg, 0);
  lv_obj_set_style_text_font(label, &lv_font_montserrat_24, 0);
  lv_obj_center(label);

  lv_obj_add_event_cb(btn, on_calc_button, LV_EVENT_CLICKED, (void *)text);
  return btn;
}

}  // namespace

void calculator_app_launch(void)
{
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
  lv_obj_set_size(back_btn, 56, 44);
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
  lv_label_set_text(title, "Calculator");
  lv_obj_set_style_text_color(title, lv_color_white(), 0);
  lv_obj_align(title, LV_ALIGN_CENTER, 0, 0);

  create_time_status_label(bar, 56);
  create_wifi_status_icon(bar, -8);

  lv_obj_t *panel = lv_obj_create(scr);
  lv_obj_set_size(panel, 300, 420);
  lv_obj_align(panel, LV_ALIGN_TOP_MID, 0, 52);
  lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_color(panel, lv_color_hex(0x11151d), 0);
  lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(panel, 0, 0);
  lv_obj_set_style_radius(panel, 16, 0);
  lv_obj_set_style_pad_all(panel, 12, 0);

  lv_obj_t *display = lv_label_create(panel);
  lv_label_set_text(display, "0");
  lv_obj_set_width(display, lv_pct(100));
  lv_obj_set_style_text_align(display, LV_TEXT_ALIGN_RIGHT, 0);
  lv_obj_set_style_text_color(display, lv_color_white(), 0);
  lv_obj_set_style_text_font(display, &lv_font_montserrat_24, 0);
  lv_obj_align(display, LV_ALIGN_TOP_RIGHT, 0, 6);

  calc_ctx.display_label = display;
  calc_ctx.input = "0";
  calc_ctx.accumulator = 0.0;
  calc_ctx.pending_op = 0;
  calc_ctx.entering_new = true;
  calc_ctx.has_error = false;

  const lv_color_t num_bg = lv_color_hex(0x2f3238);
  const lv_color_t num_fg = lv_color_white();
  const lv_color_t util_bg = lv_color_hex(0xbcc0c6);
  const lv_color_t util_fg = lv_color_black();
  const lv_color_t op_bg = lv_color_hex(0xf4a62a);
  const lv_color_t op_fg = lv_color_white();

  const lv_coord_t bw = 62;
  const lv_coord_t bh = 62;
  const lv_coord_t gap = 8;
  const lv_coord_t sx = 12;
  const lv_coord_t sy = 52;

  create_calc_btn(panel, "AC",  sx + (bw + gap) * 0, sy + (bh + gap) * 0, bw, bh, util_bg, util_fg);
  create_calc_btn(panel, "%",   sx + (bw + gap) * 1, sy + (bh + gap) * 0, bw, bh, util_bg, util_fg);
  create_calc_btn(panel, "+/-", sx + (bw + gap) * 2, sy + (bh + gap) * 0, bw, bh, util_bg, util_fg);
  create_calc_btn(panel, "/",   sx + (bw + gap) * 3, sy + (bh + gap) * 0, bw, bh, op_bg, op_fg);

  create_calc_btn(panel, "7", sx + (bw + gap) * 0, sy + (bh + gap) * 1, bw, bh, num_bg, num_fg);
  create_calc_btn(panel, "8", sx + (bw + gap) * 1, sy + (bh + gap) * 1, bw, bh, num_bg, num_fg);
  create_calc_btn(panel, "9", sx + (bw + gap) * 2, sy + (bh + gap) * 1, bw, bh, num_bg, num_fg);
  create_calc_btn(panel, "*", sx + (bw + gap) * 3, sy + (bh + gap) * 1, bw, bh, op_bg, op_fg);

  create_calc_btn(panel, "4", sx + (bw + gap) * 0, sy + (bh + gap) * 2, bw, bh, num_bg, num_fg);
  create_calc_btn(panel, "5", sx + (bw + gap) * 1, sy + (bh + gap) * 2, bw, bh, num_bg, num_fg);
  create_calc_btn(panel, "6", sx + (bw + gap) * 2, sy + (bh + gap) * 2, bw, bh, num_bg, num_fg);
  create_calc_btn(panel, "-", sx + (bw + gap) * 3, sy + (bh + gap) * 2, bw, bh, op_bg, op_fg);

  create_calc_btn(panel, "1", sx + (bw + gap) * 0, sy + (bh + gap) * 3, bw, bh, num_bg, num_fg);
  create_calc_btn(panel, "2", sx + (bw + gap) * 1, sy + (bh + gap) * 3, bw, bh, num_bg, num_fg);
  create_calc_btn(panel, "3", sx + (bw + gap) * 2, sy + (bh + gap) * 3, bw, bh, num_bg, num_fg);
  create_calc_btn(panel, "+", sx + (bw + gap) * 3, sy + (bh + gap) * 3, bw, bh, op_bg, op_fg);

  create_calc_btn(panel, "0", sx + (bw + gap) * 0, sy + (bh + gap) * 4, bw * 2 + gap, bh, num_bg, num_fg);
  create_calc_btn(panel, ".", sx + (bw + gap) * 2, sy + (bh + gap) * 4, bw, bh, num_bg, num_fg);
  create_calc_btn(panel, "=", sx + (bw + gap) * 3, sy + (bh + gap) * 4, bw, bh, op_bg, op_fg);

  ui_load_screen(scr, false);
  
  // Set route after screen is loaded
  UiSessionState::set_route("calculator");
}
