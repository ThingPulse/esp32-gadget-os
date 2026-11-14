#pragma once
#include "../../experiments/native-app/sdk/gadget_app.h"
namespace native_app::setup {
int32_t start(const void*,uint32_t,gadget_setup_callback,void*,char*,uint32_t);
void stop();
void poll();
uint32_t is_active();
}
