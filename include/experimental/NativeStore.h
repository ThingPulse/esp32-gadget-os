#pragma once
#include <ArduinoJson.h>
#if GADGET_OS_NATIVE_APP_LOADER
#include "experimental/NativePackage.h"
#include <lvgl.h>
#include <memory>
namespace native_app::store {
void usage_inventory(JsonArray);
using Buffer=std::unique_ptr<uint8_t,void(*)(void*)>;
const char *chip();
Buffer read(const char *path,Package &package,std::string &error,bool allow_unsigned=false);
std::string path(const std::string &id);
void create_tiles(lv_obj_t *grid);
void request_manager();
void close_manager();
void poll();
bool managing();
}
#endif
