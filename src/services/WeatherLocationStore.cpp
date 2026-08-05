#include "services/WeatherLocationStore.h"

#include "services/ConfigStoreService.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>

namespace {

std::string indexed_key(const char *field, int index)
{
    char key[48] = {};
    std::snprintf(key, sizeof(key), "location_%d_%s", index, field);
    return key;
}

std::string coordinate_text(double value)
{
    char text[24] = {};
    std::snprintf(text, sizeof(text), "%.6f", value);
    return text;
}

}  // namespace

bool WeatherLocationStore::load(std::vector<WeatherLocation> &locations, int &active_index)
{
    locations.clear();
    active_index = 0;
    ConfigStoreService &cfg = ConfigStoreService::instance();
    if (!cfg.begin() || !cfg.load("weather")) return false;

    int count = cfg.get_int("location_count", 0);
    if (count < 0) count = 0;
    if (count > MAX_LOCATIONS) count = MAX_LOCATIONS;
    for (int i = 0; i < count; ++i) {
        WeatherLocation location;
        const std::string lat = cfg.get(indexed_key("latitude", i).c_str(), "");
        const std::string lon = cfg.get(indexed_key("longitude", i).c_str(), "");
        if (lat.empty() || lon.empty()) continue;
        location.name = cfg.get(indexed_key("name", i).c_str(), "Location");
        location.region = cfg.get(indexed_key("region", i).c_str(), "");
        location.country = cfg.get(indexed_key("country", i).c_str(), "");
        location.timezone = cfg.get(indexed_key("timezone", i).c_str(), "auto");
        location.latitude = std::strtod(lat.c_str(), nullptr);
        location.longitude = std::strtod(lon.c_str(), nullptr);
        locations.push_back(location);
    }

    active_index = cfg.get_int("active_location", 0);
    if (active_index < 0 || active_index >= (int)locations.size()) active_index = 0;
    return true;
}

bool WeatherLocationStore::save(const std::vector<WeatherLocation> &locations, int active_index)
{
    ConfigStoreService &cfg = ConfigStoreService::instance();
    if (!cfg.begin() || !cfg.load("weather")) return false;
    const int count = (int)locations.size() < MAX_LOCATIONS
                        ? (int)locations.size() : MAX_LOCATIONS;
    if (active_index < 0 || active_index >= count) active_index = 0;
    cfg.set("location_count", count);
    cfg.set("active_location", active_index);
    for (int i = 0; i < count; ++i) {
        cfg.set(indexed_key("name", i).c_str(), locations[i].name);
        cfg.set(indexed_key("region", i).c_str(), locations[i].region);
        cfg.set(indexed_key("country", i).c_str(), locations[i].country);
        cfg.set(indexed_key("timezone", i).c_str(), locations[i].timezone);
        cfg.set(indexed_key("latitude", i).c_str(), coordinate_text(locations[i].latitude));
        cfg.set(indexed_key("longitude", i).c_str(), coordinate_text(locations[i].longitude));
    }
    return cfg.save();
}

bool WeatherLocationStore::active(WeatherLocation &location)
{
    std::vector<WeatherLocation> locations;
    int active_index = 0;
    if (!load(locations, active_index) || locations.empty()) return false;
    location = locations[active_index];
    return true;
}
