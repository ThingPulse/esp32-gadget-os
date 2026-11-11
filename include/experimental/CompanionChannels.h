#pragma once
#include <array>
#include <cstdint>
#include <string>

namespace native_app::companion {
// Transport limits, independent of all application schemas. RAM only.
constexpr size_t channel_payload_limit=2048;
constexpr size_t channel_slot_limit=8;
constexpr uint64_t channel_max_age_ms=604800000;
bool channel_name_valid(const std::string &name);
bool channel_route(const std::string &route,std::string &app,std::string &channel);
class ChannelStore {
    struct Entry {std::string app,channel,payload;uint64_t received=0;};
    std::array<Entry,channel_slot_limit> entries;
public:
    // HTTP status: 200 accepted, 400 invalid transport, 429 rate limited.
    int publish(const std::string &app,const std::string &channel,
                const char *payload,size_t size,uint64_t now);
    // Bytes copied excluding NUL, or -1 for unavailable/invalid/too small.
    // age_ms is transport residence time, never a rewrite of app data.
    int read(const std::string &app,const std::string &channel,char *out,
             uint32_t capacity,uint32_t *age_ms,uint64_t now) const;
    void clear();
};
}
