#pragma once

#include <cstdint>
#include <cstdio>
#include <string>

// Pure scheduling/identity helpers shared by the firmware and host regression tests.
namespace DeviceCheckInPolicy {
constexpr uint32_t Minute = 60U * 1000U;
constexpr uint32_t Day = 24U * 60U * Minute;
constexpr uint32_t MaxBackoff = 6U * 60U * Minute;
constexpr uint32_t MaxServerDelay = 7U * Day; // Always below the millis half-wrap interval.

inline bool due(uint32_t now, uint32_t deadline)
{
    return static_cast<int32_t>(now - deadline) >= 0;
}

inline uint32_t retry_after_ms(const std::string &seconds)
{
    if (seconds.empty()) return 0;
    uint64_t value = 0;
    for (char c : seconds) {
        if (c < '0' || c > '9') return 0;
        if (value < MaxServerDelay / 1000U) value = value * 10 + static_cast<unsigned>(c - '0');
    }
    if (value >= MaxServerDelay / 1000U) return MaxServerDelay;
    return static_cast<uint32_t>(value * 1000U);
}

inline uint32_t next_delay(int status, uint32_t server_delay, uint32_t random,
                           uint32_t &backoff)
{
    if (status == 200) {
        backoff = Minute;
        return Day + random % (15U * Minute + 1U);
    }
    // Configuration errors need an operator/firmware change, not rapid retries.
    if (status == 400 || status == 401 || status == 403 || status == 404 ||
        status == 405 || status == 413 || status == 415) backoff = MaxBackoff;
    uint32_t delay = backoff;
    backoff = backoff >= MaxBackoff / 2U ? MaxBackoff : backoff * 2U;
    if ((status == 429 || status == 503) && server_delay > delay) delay = server_delay;
    return delay + random % (Minute / 2U + 1U);
}

inline std::string uuid(uint8_t bytes[16])
{
    bytes[6] = (bytes[6] & 0x0f) | 0x40;
    bytes[8] = (bytes[8] & 0x3f) | 0x80;
    char result[37];
    std::snprintf(result, sizeof(result),
        "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
        bytes[0], bytes[1], bytes[2], bytes[3], bytes[4], bytes[5], bytes[6], bytes[7],
        bytes[8], bytes[9], bytes[10], bytes[11], bytes[12], bytes[13], bytes[14], bytes[15]);
    return result;
}

inline bool valid_uuid(const std::string &value)
{
    if (value.size() != 36 || value[14] != '4' ||
        (value[19] != '8' && value[19] != '9' && value[19] != 'a' && value[19] != 'b')) return false;
    for (size_t i = 0; i < value.size(); ++i) {
        if (i == 8 || i == 13 || i == 18 || i == 23) {
            if (value[i] != '-') return false;
        } else if (!((value[i] >= '0' && value[i] <= '9') || (value[i] >= 'a' && value[i] <= 'f'))) return false;
    }
    return true;
}
}
