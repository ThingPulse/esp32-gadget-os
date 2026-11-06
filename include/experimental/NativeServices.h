#pragma once
#include "../../experiments/native-app/sdk/gadget_app.h"
namespace native_app::services {
void begin(const char* id);
void end();
void erase_cache(const char*id);
int32_t environment(gadget_environment*);
int32_t format_time(uint32_t,uint32_t,char*,uint32_t);
uint32_t parse_time(const char*);
int32_t json_open(const char*,uint32_t);
int32_t json_get(const char*,char*,uint32_t);
int32_t json_count(const char*);
void json_close();
int32_t json_number(const char*,int32_t,int32_t*);
int32_t cache_read(void*,uint32_t);
int32_t cache_write(const void*,uint32_t);
}
