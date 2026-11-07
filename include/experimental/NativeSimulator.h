#pragma once
#include "experimental/NativeCatalog.h"
namespace native_app::simulator {
std::vector<catalog::Item> catalog();
bool install(const catalog::Item &item, std::string &error);
}
