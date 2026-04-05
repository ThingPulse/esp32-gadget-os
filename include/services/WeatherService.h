#pragma once

#include "EventBus.h"
#include "ServiceRegistry.h"
#include <string>

class WeatherService : public SystemService {
public:
    static WeatherService &instance();

    const char *name() const override { return "Weather"; }
    bool begin() override;
    void loop() override;

    bool reload_from_config();
    bool has_weather() const;
    std::string header_text() const;
    float temperature_value() const;
    int weather_code() const;
    bool is_metric() const;

private:
    static void on_wifi_connected(const SystemEvent &event, void *user_data);
    static void on_wifi_disconnected(const SystemEvent &event, void *user_data);

    bool is_wifi_connected() const;
    bool fetch_and_update();
    bool fetch_payload(std::string &out_payload) const;
    void clear_weather();
    unsigned long now_ms() const;

    bool _configured = false;
    bool _metric = true;
    double _latitude = 0.0;
    double _longitude = 0.0;
    bool _has_weather = false;
    float _temperature = 0.0f;
    int _weather_code = 0;
    unsigned long _last_fetch_ms = 0;

    SubscriptionId _wifi_connected_sub = 0;
    SubscriptionId _wifi_disconnected_sub = 0;
};