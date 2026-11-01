#pragma once
#include <lvgl.h>
#include <string>

namespace release_notes {
std::string url();
// Includes a white, four-module quiet zone around the QR symbol.
lv_obj_t *create_qr(lv_obj_t *parent);
}
