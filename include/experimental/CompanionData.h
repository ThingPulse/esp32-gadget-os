#pragma once
#include <cstdint>
#include <string>
namespace native_app::companion {
// RAM-only snapshots: reboot must never make an old observation look fresh.
class UsageSnapshot {
    std::string payload;
    uint64_t received_at=0;
public:
    bool update(const char *json, size_t size, uint64_t now);
    int read(char *output, uint32_t capacity, uint64_t now) const;
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
