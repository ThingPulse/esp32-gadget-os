#pragma once
#include <cstdint>
#if GADGET_OS_NATIVE_APP_LOADER
namespace native_app::companion {
void poll();
int32_t read_system(char *output,uint32_t capacity);
int32_t refresh_system();
void request_pairing();
bool active();
bool paired();
int32_t read_activity(char *output,uint32_t capacity);
int32_t read_usage(char *output,uint32_t capacity);
int32_t read_channel(const char *app,const char *channel,char *output,uint32_t capacity,uint32_t *age_ms);
}
#endif
