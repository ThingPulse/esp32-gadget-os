#pragma once
#if GADGET_OS_NATIVE_APP_LOADER
#include <lvgl.h>
namespace native_app::app_store {
void create_tile(lv_obj_t *grid);
void poll();
bool active();
void close();
}
#endif
