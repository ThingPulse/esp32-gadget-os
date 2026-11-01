#pragma once
#include <string>
#include <vector>
#include <cstddef>
struct AirportLocation {
    std::string icao, code;
    double latitude = 0, longitude = 0;
};
class AirportCache {
public:
    static constexpr size_t LIMIT = 64;
    void select_view(double lat, double lon, double range);
    void clear();
    bool merge(const std::vector<AirportLocation> &airports);
    const std::vector<AirportLocation> &entries() const { return entries_; }
    static bool valid(const AirportLocation &airport);
    static double distance(double lat, double lon, double other_lat, double other_lon);
private:
    bool selected_ = false;
    double lat_ = 0, lon_ = 0, range_ = 0;
    std::vector<AirportLocation> entries_;
};
