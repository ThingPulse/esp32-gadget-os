#pragma once

#include "EventBus.h"
#include "ServiceRegistry.h"
#include <string>

struct WeatherForecastDay {
    bool valid = false;
    std::string date;
    int weather_code = 0;
    float temp_max = 0.0f;
    float temp_min = 0.0f;
};

class WeatherService : public SystemService {
public:
    static constexpr int FORECAST_DAYS = 3;

    static WeatherService &instance();

    const char *name() const override { return "Weather"; }
    bool begin() override;
    void loop() override;

    bool reload_from_config();
    bool has_weather() const;
    void pause() { _enabled = false; }
    void resume() { _enabled = true; _last_fetch_ms = 0; }
    std::string header_text() const;
    float temperature_value() const;
    int weather_code() const;
    bool is_metric() const;
    float humidity_value() const;
    float pressure_value() const;
    float precipitation_value() const;
    float wind_speed_value() const;
    float wind_direction_degrees() const;
    int forecast_count() const;
    const WeatherForecastDay *forecast_days() const;
    bool refresh_now();

private:
    static void on_wifi_connected(const SystemEvent &event, void *user_data);
    static void on_wifi_disconnected(const SystemEvent &event, void *user_data);

    bool is_wifi_connected() const;
    bool fetch_and_update();
    bool fetch_payload(std::string &out_payload) const;
    void clear_weather();
    unsigned long now_ms() const;

    bool _configured = false;
    bool _enabled = false;
    bool _metric = true;
    double _latitude = 0.0;
    double _longitude = 0.0;
    bool _has_weather = false;
    float _temperature = 0.0f;
    int _weather_code = 0;
    float _humidity = 0.0f;
    float _pressure = 0.0f;
    float _precipitation = 0.0f;
    float _wind_speed = 0.0f;
    float _wind_direction = 0.0f;
    WeatherForecastDay _forecast[FORECAST_DAYS]{};
    int _forecast_count = 0;
    unsigned long _last_fetch_ms = 0;

    SubscriptionId _wifi_connected_sub = 0;
    SubscriptionId _wifi_disconnected_sub = 0;
};