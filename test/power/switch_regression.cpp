#include "hal/PowerSwitch.h"
#include <cassert>
#include <cstdint>

int main() {
  PowerSwitch s;
  assert(!s.update(true, 0));
  assert(!s.update(true, 5000)); // Holding the waking pull must not turn off.
  assert(!s.update(false, 5010));
  assert(!s.update(false, 5050)); // Release arms it, without a sleep action.
  assert(!s.update(true, 5100));
  assert(!s.update(false, 5120)); // Contact bounce: no accepted press.
  assert(!s.update(false, 5200));
  assert(!s.update(true, 5300));
  assert(!s.update(true, 5340));
  assert(!s.update(true, 9000)); // Hold never sleeps into an active wake level.
  assert(!s.update(false, 9010));
  assert(!s.update(true, 9020)); // Release bounce.
  assert(!s.update(false, 9030));
  assert(!s.update(false, 9069));
  assert(s.update(false, 9070));
  assert(!s.update(false, 9200)); // Only one event per gesture.
  assert(!s.update(true, 9300));
  assert(!s.update(true, 9340));
  assert(!s.update(false, 9400));
  assert(s.update(false, 9440)); // Can retry after an ignored OTA gesture.

  PowerSwitch wrap;
  assert(!wrap.update(false, UINT32_MAX - 100));
  assert(!wrap.update(false, UINT32_MAX - 60));
  assert(!wrap.update(true, UINT32_MAX - 30));
  assert(!wrap.update(true, 10));
  assert(!wrap.update(false, 20));
  assert(wrap.update(false, 60));
}
