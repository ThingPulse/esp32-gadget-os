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
    std::string dialog_icao;
    std::array<lv_obj_t *, 2> rows{}, row_labels{};
    std::array<int, PlanesService::MAX_PLANES> indices{};
    std::array<int, 2> alternatives{{-1, -1}};
    int count = 0;
    double radius_km = 10;
    std::string selected_icao;
    struct TrailPoint { float east_km, north_km; };
    struct AircraftTrail {
        char icao[7]{};
        uint8_t count = 0, next = 0;
        std::array<TrailPoint, 20> points{};
        const TrailPoint &point(int index) const {
            return points[(next + points.size() - count + index) % points.size()];
        }
    };
    std::array<AircraftTrail, PlanesService::MAX_PLANES> trails{};
    double trail_latitude = 0, trail_longitude = 0;
    void (*range_changed)() = nullptr;
};
void nearby_planes_create(NearbyPlanesView &view, lv_obj_t *parent);
void nearby_planes_update(NearbyPlanesView &view);
void nearby_planes_tick(NearbyPlanesView &view);
