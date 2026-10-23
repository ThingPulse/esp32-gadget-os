#pragma once

#include "EventBus.h"
#include "ServiceRegistry.h"
#include <string>
#include <ctime>

struct WeatherForecastDay {
    bool valid = false;
    std::string date;
    int weather_code = 0;
    float temp_max = 0.0f;
    float temp_min = 0.0f;
};

struct WeatherForecastHour {
    bool valid = false;
    std::string time;
    int weather_code = 0;
    float temperature = 0.0f;
};

class WeatherService : public SystemService {
public:
    static constexpr int FORECAST_DAYS = 4;
    static constexpr int FORECAST_HOURS = 12;

    static WeatherService &instance();

    const char *name() const override { return "Weather"; }
    bool begin() override;
    void loop() override;

    bool reload_from_config();
    bool has_weather() const;
    bool local_time(time_t utc, tm &result) const;
    bool is_fetching() const { return _fetch_in_progress; }
    unsigned long data_revision() const { return _data_revision; }
    void pause() { _enabled = false; }
    void resume() { _enabled = true; }
    std::string header_text() const;
    float temperature_value() const;
    int weather_code() const;
    bool is_metric() const;
    float humidity_value() const;
    float pressure_value() const; // hPa in metric mode, inHg otherwise.
    float precipitation_value() const;
    float wind_speed_value() const;
    float wind_direction_degrees() const;
    const std::string &sunrise_time() const { return _sunrise; }
    const std::string &sunset_time() const { return _sunset; }
    float moon_phase_fraction() const { return _moon_phase; }
    double latitude() const { return _latitude; }
    double longitude() const { return _longitude; }
    float moon_illumination_percent() const;
    const char *moon_phase_name() const;
    int forecast_count() const;
    const WeatherForecastDay *forecast_days() const;
    int hourly_forecast_count() const;
    const WeatherForecastHour *forecast_hours() const;
    bool refresh_now();

private:
    static void on_wifi_connected(const SystemEvent &event, void *user_data);
    static void on_wifi_disconnected(const SystemEvent &event, void *user_data);

    bool is_wifi_connected() const;
    bool fetch_and_update();
    bool apply_payload(std::string &payload);
    bool fetch_payload(std::string &out_payload) const;
    bool start_async_fetch();
    void process_async_result();
#if defined(ARDUINO)
    static void fetch_task_entry(void *user_data);
#endif
    void clear_weather();
    unsigned long now_ms() const;

    bool _configured = false;
    bool _enabled = false;
    bool _metric = true;
    double _latitude = 0.0;
    double _longitude = 0.0;
    bool _has_weather = false;
    bool _has_utc_offset = false;
    int _utc_offset_seconds = 0;
    float _temperature = 0.0f;
    int _weather_code = 0;
    float _humidity = 0.0f;
    float _pressure = 0.0f;
    float _precipitation = 0.0f;
    float _wind_speed = 0.0f;
    float _wind_direction = 0.0f;
    std::string _sunrise;
    std::string _sunset;
    float _moon_phase = 0.0f;
    WeatherForecastDay _forecast[FORECAST_DAYS]{};
    WeatherForecastHour _hourly_forecast[FORECAST_HOURS]{};
    int _forecast_count = 0;
    int _hourly_forecast_count = 0;
    unsigned long _last_fetch_ms = 0;
    unsigned long _data_revision = 0;
    volatile bool _fetch_in_progress = false;

    SubscriptionId _wifi_connected_sub = 0;
    SubscriptionId _wifi_disconnected_sub = 0;
};
