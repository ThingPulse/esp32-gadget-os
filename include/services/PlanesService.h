#pragma once

#include "EventBus.h"
#include "ServiceRegistry.h"

#include <string>
#include <unordered_map>

struct PlaneState {
    bool valid = false;
    std::string icao24;
    std::string callsign;
    std::string route;
    float latitude = 0.0f;
    float longitude = 0.0f;
    float altitude_m = 0.0f;
    float velocity_ms = 0.0f;
    bool on_ground = false;
};

class PlanesService : public SystemService {
public:
    static constexpr int MAX_PLANES = 20;

    static PlanesService &instance();

    const char *name() const override { return "Planes"; }
    bool begin() override;
    void loop() override;

    bool reload_from_config();
    bool refresh_now();
    void pause() { _enabled = false; }
    void resume() { _enabled = true; _last_fetch_ms = 0; }

    bool is_configured() const { return _configured; }
    bool has_data() const { return _has_data; }
    int plane_count() const { return _plane_count; }
    const PlaneState *planes() const { return _planes; }

    double center_latitude() const { return _center_lat; }
    double center_longitude() const { return _center_lon; }
    double range_km() const { return _range_km; }
    bool is_metric() const { return _metric; }
    bool hide_ground() const { return _hide_ground; }
    const std::string &last_error() const { return _last_error; }

private:
    static void on_wifi_connected(const SystemEvent &event, void *user_data);
    static void on_wifi_disconnected(const SystemEvent &event, void *user_data);

    bool is_wifi_connected() const;
    bool fetch_and_update();
    bool fetch_payload(std::string &out_payload);
    bool fetch_route_for_callsign(const std::string &callsign_key, std::string &out_route);
    std::string route_for_callsign(const std::string &callsign_raw, int &network_budget);
    void clear_planes();
    unsigned long now_ms() const;

    struct RouteCacheEntry {
        std::string route;
        unsigned long expires_at_ms = 0;
        RouteCacheEntry() = default;
        RouteCacheEntry(const std::string &r, unsigned long e) : route(r), expires_at_ms(e) {}
    };

    bool _configured = false;
    bool _has_data = false;
    bool _enabled = false;
    bool _metric = true;
    bool _hide_ground = false;
    double _center_lat = 0.0;
    double _center_lon = 0.0;
    double _range_km = 30.0;
    PlaneState _planes[MAX_PLANES]{};
    int _plane_count = 0;
    unsigned long _last_fetch_ms = 0;
    std::string _last_error;
    std::unordered_map<std::string, RouteCacheEntry> _route_cache;

    SubscriptionId _wifi_connected_sub = 0;
    SubscriptionId _wifi_disconnected_sub = 0;
};