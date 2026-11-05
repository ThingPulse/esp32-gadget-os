#pragma once
#include <string>
#include <vector>
#include <cstddef>
namespace native_app::catalog {
struct Item {
    std::string id, name, version, description, url;
    size_t bytes=0;
};
// Deliberately accept only canonical absolute URLs inside the catalog directory.
bool allowed_url(const std::string &url, const std::string &catalog_url);
bool parse(const char *data,size_t size,const char *chip,const std::string &base,
           std::vector<Item> &items,std::string &error);
}
