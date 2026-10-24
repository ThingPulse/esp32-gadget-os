#pragma once

#include <cstdint>

// Main-loop owned; the worker only reports whether a check succeeded.
class UpdateCheckSchedule {
public:
    static constexpr uint32_t Minute = 60000U;
    static constexpr uint32_t Day = 24U * 60U * Minute;
    static constexpr uint32_t MaxBackoff = 6U * 60U * Minute;

    bool due(uint32_t now)
    {
        // Latch expiry while offline, including across millis() rollover.
        if (static_cast<int32_t>(now - _deadline) >= 0) _due = true;
        return _due;
    }

    void completed(uint32_t now, bool success, uint32_t random)
    {
        uint32_t delay;
        if (success) {
            _backoff = Minute;
            delay = Day + random % (15U * Minute + 1U);
        } else {
            delay = _backoff + random % (30000U + 1U);
            _backoff = _backoff >= MaxBackoff / 2U ? MaxBackoff : _backoff * 2U;
        }
        _deadline = now + delay;
        _due = false;
    }

private:
    uint32_t _deadline = 0;
    uint32_t _backoff = Minute;
    bool _due = true; // First Wi-Fi connection after boot checks immediately.
};
