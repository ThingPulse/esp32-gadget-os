#pragma once
#include <cmath>
#include <cstdint>

namespace PlaneAltitudeColor {
// Fixed physical thresholds in metres; display unit changes never alter colors.
// Zero denotes unavailable altitude, followed by five low-to-high bands.
inline uint8_t band(float altitude_m) {
    if (!std::isfinite(altitude_m)) return 0;
    if (altitude_m < 2000) return 1;
    if (altitude_m < 4000) return 2;
    if (altitude_m < 6000) return 3;
    if (altitude_m < 9000) return 4;
    return 5;
}
inline uint32_t color(uint8_t band) {
    static constexpr uint32_t palette[] = {
        0x9aa5bd, 0xffc76b, 0x83e377, 0x56d9ed, 0x6da9ff, 0xcf94ff
    };
    return palette[band <= 5 ? band : 0];
}
}
