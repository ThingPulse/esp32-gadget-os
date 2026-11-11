#pragma once
#include <cstdint>
#include <string>
namespace native_app::companion {
// Frozen SDK 1.3–1.6 compatibility adapters only. New app schemas belong in
// their installable sources; use CompanionChannels for schema-free transport.
class UsageSnapshot {
    std::string payload;
    uint64_t received_at=0;
public:
    bool update(const char *json, size_t size, uint64_t now);
    int read(char *output, uint32_t capacity, uint64_t now) const;
    void clear(){payload.clear();}
};
class ActivitySnapshot {
    std::string payload;
    uint64_t received_at=0;
public:
    bool update(const char *json,size_t size,uint64_t now);
    int read(char *output,uint32_t capacity,uint64_t now) const;
    void clear(){payload.clear();}
};
class SystemSnapshot {
    std::string payload;
    uint64_t received_at=0;
public:
    bool update(const char *json,size_t size,uint64_t now);
    int read(char *output,uint32_t capacity,uint64_t now) const;
    void clear(){payload.clear();}
};
}
