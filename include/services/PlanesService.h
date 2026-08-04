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
    std::string registration;
    std::string aircraft_type;
    std::string aircraft_description;
    std::string origin_country;
    std::string category;
    std::string squawk;
    float latitude = 0.0f;
    float longitude = 0.0f;
    float altitude_m = 0.0f;
    float velocity_ms = 0.0f;
    float heading_deg = 0.0f;
    float vertical_rate_ms = 0.0f;
    float distance_km = 0.0f;
    float bearing_deg = 0.0f;
    bool on_ground = false;
    bool military = false;
    bool heading_valid = false;
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
    void resume() { _enabled = true; }

    bool is_configured() const { return _configured; }
    bool has_data() const { return _has_data; }
    bool is_fetching() const { return _fetch_in_progress; }
    unsigned long data_revision() const { return _data_revision; }
    int plane_count() const { return _plane_count; }
    const PlaneState *planes() const { return _planes; }
    const std::string &provider_name() const { return _provider_name; }

    double center_latitude() const { return _center_lat; }
    double center_longitude() const { return _center_lon; }
    double range_km() const { return _range_km; }
    bool is_metric() const { return _metric; }
    bool hide_ground() const { return _hide_ground; }
    int rate_limit_remaining() const { return _rate_limit_remaining; }
    const std::string &last_error() const { return _last_error; }
    bool route_airport_names(const std::string &callsign, std::string &origin,
                             std::string &destination) const;

private:
    static void on_wifi_connected(const SystemEvent &event, void *user_data);
    static void on_wifi_disconnected(const SystemEvent &event, void *user_data);

    bool is_wifi_connected() const;
    bool fetch_and_update();
    bool fetch_payload(std::string &out_payload, std::string &out_provider, std::string &out_error);
    bool parse_payload(const std::string &payload, PlaneState *out_planes, int &out_count, std::string &out_error);
    void enrich_routes(PlaneState *planes, int count);
    void publish_result(PlaneState *planes, int count, const std::string &provider);
    bool fetch_route_for_callsign(const std::string &callsign_key, std::string &out_route,
                                  std::string &out_origin_name, std::string &out_destination_name);
    std::string route_for_callsign(const std::string &callsign_raw, int &network_budget,
                                   std::string &out_origin_name, std::string &out_destination_name);
    bool start_async_fetch();
    void process_async_result();
#if defined(ARDUINO)
    static void fetch_task_entry(void *user_data);
#endif
    void clear_planes();
    unsigned long now_ms() const;

    struct RouteCacheEntry {
        std::string route;
        std::string origin_name;
        std::string destination_name;
        unsigned long expires_at_ms = 0;
        RouteCacheEntry() = default;
        RouteCacheEntry(const std::string &r, const std::string &origin,
                        const std::string &destination, unsigned long e)
            : route(r), origin_name(origin), destination_name(destination), expires_at_ms(e) {}
    };

    bool _configured = false;
    bool _has_data = false;
    bool _enabled = false;
    bool _metric = true;
    bool _hide_ground = false;
    int _rate_limit_remaining = -1;
    double _center_lat = 0.0;
    double _center_lon = 0.0;
    double _range_km = 30.0;
    PlaneState _planes[MAX_PLANES]{};
    int _plane_count = 0;
    unsigned long _last_fetch_ms = 0;
    unsigned long _data_revision = 0;
    volatile bool _fetch_in_progress = false;
    std::string _provider_name;
    std::string _last_error;
    std::unordered_map<std::string, RouteCacheEntry> _route_cache;

    SubscriptionId _wifi_connected_sub = 0;
    SubscriptionId _wifi_disconnected_sub = 0;
};
