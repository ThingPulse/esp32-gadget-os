#pragma once
#include <lvgl.h>
#include "../../experiments/native-app/sdk/gadget_app.h"
namespace native_app { namespace ui {
// Shared implementation for Xtensa and SDL. begin/end are host-only.
void begin(lv_obj_t *screen, void (*dispatch)(gadget_callback,void*));
void end();
void suspend();
extern const gadget_ui_api api;
} }
