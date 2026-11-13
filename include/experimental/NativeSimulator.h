#pragma once
#include "experimental/NativeCatalog.h"
namespace native_app::simulator {
// A desktop adapter opts into the settings contract explicitly. Its settings
// entry must return to the active caller screen on Close, like normal previews.
void register_settings_adapter(const char *id, void (*launch)());
std::vector<catalog::Item> catalog();
bool install(const catalog::Item &item, std::string &error);
}
