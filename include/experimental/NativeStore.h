#pragma once
#include <ArduinoJson.h>
#if GADGET_OS_NATIVE_APP_LOADER
#include "experimental/NativePackage.h"
#include <lvgl.h>
#include <memory>
#include <vector>
namespace native_app::store {
struct Installed {Manifest manifest;bool trusted;};
std::vector<Installed> installed();
bool uninstall(const std::string &id,std::string &error);
bool install_signed(const uint8_t *data,size_t size,const Manifest &expected,std::string &error);
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
