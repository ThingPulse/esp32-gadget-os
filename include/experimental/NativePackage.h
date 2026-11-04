#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
namespace native_app {
constexpr size_t max_elf_bytes = 64 * 1024;
struct Manifest { std::string id, version, executable, name; };
constexpr size_t package_header_bytes=52, package_icon_bytes=3200;
constexpr size_t max_package_bytes=package_header_bytes+2048+package_icon_bytes+max_elf_bytes;
struct Package { Manifest manifest; size_t icon_offset=0, elf_offset=0, elf_size=0; };
bool valid_app_id(const std::string &id);
bool validate_package(const uint8_t *,size_t,const char *,Package &,std::string &);
bool parse_manifest(const std::string &json, const char *chip, Manifest &out, std::string &error);
bool validate_elf(const uint8_t *bytes, size_t size, std::string &error);
/* Shared by the real host and lifecycle regression. All access is UI-thread only. */
struct DeliveryGate {
    bool closing = false;
    unsigned depth = 0;
    bool enter() { if (closing) return false; ++depth; return true; }
    void leave() { --depth; }
    void close() { closing = true; }
    bool can_unload() const { return closing && depth == 0; }
};
}
