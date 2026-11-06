#pragma once
#include <cstdint>
#if GADGET_OS_NATIVE_APP_LOADER
namespace native_app::companion {
void poll();
int32_t read_system(char *output,uint32_t capacity);
int32_t refresh_system();
void request_pairing();
bool active();
int32_t read_usage(char *output,uint32_t capacity);
}
#endif
