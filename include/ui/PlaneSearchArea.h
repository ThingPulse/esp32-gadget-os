#pragma once
#include <algorithm>
#include <cmath>

struct PlaneSearchArea {
    double latitude = 0;
    double longitude = 0;
    double range_km = 30;

    void pan(int north, int east) {
        const double step_km = range_km * 0.25;
        const double cosine = std::max(0.087, std::cos(latitude * 3.14159265358979323846 / 180.0));
        latitude = std::max(-85.0, std::min(85.0, latitude + north * step_km / 111.32));
        longitude = std::fmod(longitude + east * step_km / (111.32 * cosine) + 540.0, 360.0) - 180.0;
    }
    void zoom(bool in) {
        // Providers accept at most 250 nautical miles.
        range_km = std::max(1.0, std::min(463.0, range_km * (in ? 0.8 : 1.25)));
    }
};
