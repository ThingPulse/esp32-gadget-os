#pragma once
#include <stdint.h>
namespace native_app { namespace settings {
void begin(const char *app_id);
void end();
int32_t get(const char *key,char *value,uint32_t capacity);
int32_t set(const char *key,const char *value);
} }
