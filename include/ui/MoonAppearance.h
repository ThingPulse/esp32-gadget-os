#pragma once
#include <algorithm>
#include <cmath>
#include <ctime>

// Low precision Sun/Moon ephemeris adapted from SunCalc 1.9.0 (BSD-2-Clause).
// https://github.com/mourner/suncalc/tree/v1.9.0
// Copyright (c) 2014, Vladimir Agafonkin. See docs/licenses/SunCalc.txt.
// Suitable for a small weather icon, not telescope pointing. Ignores libration.
namespace moon_appearance {
constexpr double pi = 3.14159265358979323846;
constexpr double rad = pi / 180;
struct Appearance {
    double phase;               // 0=new, .25=first quarter, .5=full
    double illumination;        // 0..1
    double bright_angle;        // from zenith, towards celestial east (radians)
    double parallactic_angle;  // rotates the reference texture into observer view
};
struct Equatorial { double ra, dec; };
inline Equatorial equatorial(double longitude, double latitude) {
    const double e = 23.4397 * rad;
    return {std::atan2(std::sin(longitude) * std::cos(e) - std::tan(latitude) * std::sin(e),
                       std::cos(longitude)),
            std::asin(std::sin(latitude) * std::cos(e) +
                      std::cos(latitude) * std::sin(e) * std::sin(longitude))};
}
inline Appearance calculate(time_t utc, double latitude, double longitude) {
    const double d = double(utc) / 86400.0 + 2440587.5 - 2451545.0;
    const double solar_anomaly = rad * (357.5291 + .98560028 * d);
    const double solar_longitude = solar_anomaly + rad * (102.9372 +
        1.9148 * std::sin(solar_anomaly) + .02 * std::sin(2 * solar_anomaly) +
        .0003 * std::sin(3 * solar_anomaly)) + pi;
    const auto sun = equatorial(solar_longitude, 0);
    const double anomaly = rad * (134.963 + 13.064993 * d);
    const auto moon = equatorial(rad * (218.316 + 13.176396 * d + 6.289 * std::sin(anomaly)),
                                rad * 5.128 * std::sin(rad * (93.272 + 13.229350 * d)));
    const double separation = std::acos(std::max(-1.0, std::min(1.0,
        std::sin(sun.dec) * std::sin(moon.dec) +
        std::cos(sun.dec) * std::cos(moon.dec) * std::cos(sun.ra - moon.ra))));
    const double incidence = std::atan2(149598000 * std::sin(separation),
        385001 - 20905 * std::cos(anomaly) - 149598000 * std::cos(separation));
    const double limb = std::atan2(std::cos(sun.dec) * std::sin(sun.ra - moon.ra),
        std::sin(sun.dec) * std::cos(moon.dec) -
        std::cos(sun.dec) * std::sin(moon.dec) * std::cos(sun.ra - moon.ra));
    const double h = rad * (280.16 + 360.9856235 * d + longitude) - moon.ra;
    const double lat = latitude * rad;
    // atan2 form without tan(latitude) remains finite at the poles.
    const double q = std::atan2(std::sin(h) * std::cos(lat),
        std::sin(lat) * std::cos(moon.dec) - std::cos(lat) * std::sin(moon.dec) * std::cos(h));
    return {.5 + .5 * incidence * (limb < 0 ? -1 : 1) / pi,
            (1 + std::cos(incidence)) / 2, limb - q, q};
}
inline const char *phase_name(double phase) {
    static const char *names[] = {"New moon", "Waxing crescent", "First quarter", "Waxing gibbous",
        "Full moon", "Waning gibbous", "Last quarter", "Waning crescent"};
    return names[int(std::floor(phase * 8 + .5)) & 7];
}
} // namespace moon_appearance
