#pragma once
#include <string>
#if GADGET_OS_NATIVE_APP_LOADER || GADGET_OS_APP_STORE_SIMULATOR
namespace native_app::companion_setup {
bool required(const std::string &id);
void request(const char *resume_app=nullptr);
bool active();
void poll();
}
#endif
