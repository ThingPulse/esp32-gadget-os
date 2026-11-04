#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
namespace native_app {
constexpr size_t max_elf_bytes = 64 * 1024;
struct Manifest { std::string id, version, executable; };
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
