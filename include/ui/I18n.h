#pragma once
#include <lvgl.h>
#include <ctime>

namespace i18n {
// Stable English messages are catalogue keys. Unknown keys fall back to English.
const char *tr(const char *english);
bool german();
const char *language_code();
void init();
bool set_language(const char *code);
bool take_reload_request();
const lv_font_t *font14();
const lv_font_t *font24();
void apply_fonts(lv_display_t *display);
const char *weekday(int day, bool abbreviated=false);
void date(char *buffer, size_t size, const tm &value);
}
