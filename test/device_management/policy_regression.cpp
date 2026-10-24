#include "services/DeviceCheckInPolicy.h"
#include <cassert>
#include <iostream>
#include <limits>

using namespace DeviceCheckInPolicy;
int main()
{
    uint32_t backoff = Minute;
    assert(next_delay(-1, 0, 0, backoff) == Minute);
    assert(next_delay(502, 0, 0, backoff) == 2 * Minute);
    assert(next_delay(503, 0, 0, backoff) == 4 * Minute);
    for (int i = 0; i < 20; ++i) next_delay(-1, 0, 0, backoff);
    assert(backoff == MaxBackoff);
    assert(next_delay(-1, 0, 1000, backoff) == MaxBackoff + 1000);
    assert(next_delay(200, 0, 0, backoff) == Day);
    assert(backoff == Minute);
    assert(next_delay(429, 300000, 0, backoff) == 300000);
    assert(next_delay(503, 2 * Day, 1000, backoff) == 2 * Day + 1000);
    assert(next_delay(401, 0, 0, backoff) == MaxBackoff);
    assert(next_delay(404, 0, 0, backoff) == MaxBackoff);
    const auto daily = next_delay(200, 0, std::numeric_limits<uint32_t>::max(), backoff);
    assert(daily >= Day && daily <= Day + 15 * Minute);

    assert(retry_after_ms("300") == 300000);
    assert(retry_after_ms("0") == 0);
    assert(retry_after_ms("-1") == 0);
    assert(retry_after_ms("1.5") == 0);
    assert(retry_after_ms("") == 0);
    assert(retry_after_ms("9999999999999999999999999999") == MaxServerDelay);
    assert(retry_after_ms("9999999999999bad") == 0);

    // Reboots reset the scheduler, but millis wrap must not trigger early retries.
    const uint32_t before_wrap = 0xfffffff0U;
    const uint32_t deadline = before_wrap + Minute;
    assert(!due(before_wrap, deadline));
    assert(!due(deadline - 1, deadline));
    assert(due(deadline, deadline));
    assert(due(deadline + 1, deadline));

    uint8_t bytes[16] = {};
    const auto id = uuid(bytes);
    assert(id == "00000000-0000-4000-8000-000000000000");
    assert(valid_uuid(id));
    bytes[0] = 255;
    assert(uuid(bytes) != id);
    assert(valid_uuid(uuid(bytes)));
    assert(!valid_uuid("00000000-0000-1000-8000-000000000000"));
    assert(!valid_uuid("00000000-0000-4000-f000-000000000000"));
    assert(!valid_uuid("00:11:22:33:44:55"));
    std::cout << "Device check-in policy regressions passed\n";
}
