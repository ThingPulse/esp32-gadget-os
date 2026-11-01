#pragma once
#include <cstdint>

// Active-high momentary switch. Require an initial stable release, then a
// debounced press/release cycle. In particular, ignore a held wake-up gesture.
class PowerSwitch {
 public:
  bool update(bool high, uint32_t now) {
    if (!initialized_) {
      initialized_ = true;
      raw_ = high;
      changed_ = now;
    }
    if (high != raw_) { raw_ = high; changed_ = now; }
    if (uint32_t(now - changed_) < 40) return false;
    if (!armed_) {
      if (!high) armed_ = true;
      return false;
    }
    if (high) pressed_ = true;
    else if (pressed_) { pressed_ = false; return true; }
    return false;
  }
 private:
  bool initialized_ = false, raw_ = false, armed_ = false, pressed_ = false;
  uint32_t changed_ = 0;
};
