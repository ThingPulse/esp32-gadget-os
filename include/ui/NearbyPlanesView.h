#pragma once
#include <lvgl.h>
#include <string>
#include <array>
#include "services/PlanesService.h"

struct NearbyPlanesView {
    lv_obj_t *root = nullptr, *radar = nullptr, *summary = nullptr, *range_label = nullptr;
    lv_obj_t *card = nullptr, *look = nullptr, *distance = nullptr, *identity = nullptr, *type = nullptr, *route = nullptr;
    lv_obj_t *status = nullptr;
    lv_obj_t *previous = nullptr, *next = nullptr, *page = nullptr;
    lv_obj_t *dialog = nullptr, *dialog_title = nullptr, *dialog_body = nullptr, *dialog_close = nullptr;
    lv_obj_t *dialog_content = nullptr, *dialog_airline = nullptr, *dialog_type = nullptr;
    lv_obj_t *dialog_route = nullptr, *dialog_route_missing = nullptr;
    std::array<lv_obj_t *, 2> dialog_codes{}, dialog_airports{};
    std::array<lv_obj_t *, 4> dialog_stats{};
    lv_obj_t *dialog_registration = nullptr, *dialog_hex = nullptr, *dialog_squawk = nullptr;
    std::string dialog_icao;
    std::array<int, PlanesService::MAX_PLANES> indices{};
    int count = 0;
    double radius_km = 25;
    std::string selected_icao;
    struct TrailPoint { float east_km, north_km; };
    struct AircraftTrail {
        char icao[7]{};
        uint8_t count = 0, next = 0;
        std::array<TrailPoint, 20> points{};
        // Parallel byte array avoids padding every pair of float coordinates.
        std::array<uint8_t, 20> altitude_bands{};
        const TrailPoint &point(int index) const {
            return points[(next + points.size() - count + index) % points.size()];
        }
        uint8_t altitude_band(int index) const {
            return altitude_bands[(next + points.size() - count + index) % points.size()];
        }
    };
    std::array<AircraftTrail, PlanesService::MAX_PLANES> trails{};
    double trail_latitude = 0, trail_longitude = 0;
    void (*range_changed)() = nullptr;
};
void nearby_planes_create(NearbyPlanesView &view, lv_obj_t *parent);
void nearby_planes_update(NearbyPlanesView &view);
void nearby_planes_tick(NearbyPlanesView &view);
