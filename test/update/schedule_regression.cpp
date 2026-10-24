#include "services/UpdateCheckSchedule.h"
#include <cassert>
#include <cstdio>
#include <limits>

int main()
{
    using Schedule = UpdateCheckSchedule;
    Schedule schedule;
    assert(schedule.due(0));
    schedule.completed(100, true, 0);
    assert(!schedule.due(100 + Schedule::Day - 1));
    assert(schedule.due(100 + Schedule::Day));

    // Jitter reaches its upper bound, including for manually initiated checks.
    schedule.completed(0, true, 15 * Schedule::Minute);
    assert(!schedule.due(Schedule::Day + 15 * Schedule::Minute - 1));
    assert(schedule.due(Schedule::Day + 15 * Schedule::Minute));

    // Failure backoff doubles and stays capped. Success resets it.
    uint32_t now = 0;
    uint32_t delay = Schedule::Minute;
    for (int i = 0; i < 20; ++i) {
        schedule.completed(now, false, 30000);
        assert(!schedule.due(now + delay + 29999));
        now += delay + 30000;
        assert(schedule.due(now));
        delay = delay >= Schedule::MaxBackoff / 2 ? Schedule::MaxBackoff : delay * 2;
    }
    schedule.completed(now, true, 0);
    schedule.completed(now, false, 0);
    assert(!schedule.due(now + Schedule::Minute - 1));
    assert(schedule.due(now + Schedule::Minute));

    // A deadline may wrap; going offline must not lose an already due check.
    now = std::numeric_limits<uint32_t>::max() - 1000;
    schedule.completed(now, false, 0);
    assert(!schedule.due(now));
    assert(!schedule.due(now + 2 * Schedule::Minute - 1));
    assert(schedule.due(now + 2 * Schedule::Minute));
    assert(schedule.due(now + 0x90000000U));
    schedule.completed(42, true, 0);
    assert(!schedule.due(42));
    std::puts("OTA schedule regressions passed");
}
