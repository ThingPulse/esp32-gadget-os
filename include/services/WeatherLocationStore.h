#pragma once

#include <string>
#include <vector>

struct WeatherLocation {
    std::string name;
    std::string region;
    std::string country;
    std::string timezone;
    double latitude = 0.0;
    double longitude = 0.0;
};

class WeatherLocationStore {
public:
    static constexpr int MAX_LOCATIONS = 5;

    static bool load(std::vector<WeatherLocation> &locations, int &active_index);
    static bool save(const std::vector<WeatherLocation> &locations, int active_index);
    static bool active(WeatherLocation &location);
};
