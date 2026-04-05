#include "services/WeatherService.h"

#include "EventBus.h"
#include "services/ConfigStoreService.h"
#include "services/WifiService.h"

#include <ArduinoJson.h>
#include <cstdio>
#include <cstdlib>

#if defined(ARDUINO)
#include <Arduino.h>
#include <HTTPClient.h>
#include <WiFiClient.h>
#include <WiFi.h>
#else
#include <chrono>
#endif

namespace {

constexpr unsigned long WEATHER_REFRESH_MS = 10UL * 60UL * 1000UL;

const char *icon_for_code(int weather_code)
{
    if (weather_code == 0) return "SUN";
    if (weather_code <= 3) return "CLD";
    if (weather_code >= 45 && weather_code <= 48) return "FOG";
    if (weather_code >= 51 && weather_code <= 67) return "RAN";
    if (weather_code >= 71 && weather_code <= 77) return "SNW";
    if (weather_code >= 80 && weather_code <= 82) return "RAN";
    if (weather_code >= 95) return "STM";
    return "CLD";
}

}  // namespace

WeatherService &WeatherService::instance()
{
    static WeatherService inst;
    return inst;
}

bool WeatherService::begin()
{
    if (_wifi_connected_sub == 0)
        _wifi_connected_sub = EventBus::instance().subscribe(EventType::WifiConnected, on_wifi_connected, this);
    if (_wifi_disconnected_sub == 0)
        _wifi_disconnected_sub = EventBus::instance().subscribe(EventType::WifiDisconnected, on_wifi_disconnected, this);

    reload_from_config();
    if (_configured && is_wifi_connected())
        fetch_and_update();

    return true;
}

void WeatherService::loop()
{
    if (!_configured || !is_wifi_connected())
        return;

    if (!_has_weather || (now_ms() - _last_fetch_ms >= WEATHER_REFRESH_MS))
        fetch_and_update();
}

bool WeatherService::reload_from_config()
{
    ConfigStoreService &cfg = ConfigStoreService::instance();
    if (!cfg.begin()) {
        clear_weather();
        return false;
    }

    cfg.load("weather");
    std::string lat = cfg.get("latitude", "");
    std::string lon = cfg.get("longitude", "");
    _metric = cfg.get_bool("metric", true);

    if (lat.empty() || lon.empty()) {
        _configured = false;
        clear_weather();
        return false;
    }

    _latitude = std::strtod(lat.c_str(), nullptr);
    _longitude = std::strtod(lon.c_str(), nullptr);
    _configured = true;

    if (is_wifi_connected())
        fetch_and_update();

    return true;
}

bool WeatherService::has_weather() const
{
    return _has_weather;
}

float WeatherService::temperature_value() const
{
    return _temperature;
}

int WeatherService::weather_code() const
{
    return _weather_code;
}

bool WeatherService::is_metric() const
{
    return _metric;
}

std::string WeatherService::header_text() const
{
    if (!_has_weather)
        return "";

    char buf[48] = {0};
    const char unit = _metric ? 'C' : 'F';
    std::snprintf(buf, sizeof(buf), "%s %.1f%c", icon_for_code(_weather_code), _temperature, unit);
    return std::string(buf);
}

void WeatherService::on_wifi_connected(const SystemEvent &event, void *user_data)
{
    (void)event;
    WeatherService *self = static_cast<WeatherService *>(user_data);
    if (self && self->_configured)
        self->fetch_and_update();
}

void WeatherService::on_wifi_disconnected(const SystemEvent &event, void *user_data)
{
    (void)event;
    WeatherService *self = static_cast<WeatherService *>(user_data);
    if (self)
        self->clear_weather();
}

bool WeatherService::is_wifi_connected() const
{
    return WifiService::instance().is_connected();
}

bool WeatherService::fetch_and_update()
{
    std::string payload;
    if (!fetch_payload(payload)) {
        clear_weather();
        return false;
    }

    JsonDocument doc;
    if (deserializeJson(doc, payload.c_str(), payload.size()) != DeserializationError::Ok) {
        clear_weather();
        return false;
    }

    JsonVariant current = doc["current"];
    if (!current.isNull()) {
        _temperature = current["temperature_2m"].as<float>();
        _weather_code = current["weather_code"].as<int>();
    } else {
        JsonVariant current_weather = doc["current_weather"];
        _temperature = current_weather["temperature"].as<float>();
        _weather_code = current_weather["weathercode"].as<int>();
    }

    _has_weather = true;
    _last_fetch_ms = now_ms();
    EventBus::instance().publish(SystemEvent{EventType::WeatherUpdated});
    return true;
}

bool WeatherService::fetch_payload(std::string &out_payload) const
{
    char url[320] = {0};
    const char *temp_unit = _metric ? "celsius" : "fahrenheit";
    std::snprintf(url,
                  sizeof(url),
                  "http://api.open-meteo.com/v1/forecast?latitude=%.6f&longitude=%.6f&current=temperature_2m,weather_code&hourly=temperature_2m&temperature_unit=%s",
                  _latitude,
                  _longitude,
                  temp_unit);

#if defined(ARDUINO)
    WiFiClient client;
    HTTPClient http;
    if (!http.begin(client, url))
        return false;

    int code = http.GET();
    if (code != HTTP_CODE_OK) {
        http.end();
        return false;
    }

    out_payload = http.getString().c_str();
    http.end();
    return !out_payload.empty();
#else
    char cmd[420] = {0};
    std::snprintf(cmd, sizeof(cmd), "curl -s --max-time 8 \"%s\"", url);

    FILE *pipe = popen(cmd, "r");
    if (!pipe)
        return false;

    out_payload.clear();
    char buffer[256];
    while (fgets(buffer, sizeof(buffer), pipe))
        out_payload += buffer;

    int rc = pclose(pipe);
    return rc == 0 && !out_payload.empty();
#endif
}

void WeatherService::clear_weather()
{
    if (!_has_weather)
        return;

    _has_weather = false;
    EventBus::instance().publish(SystemEvent{EventType::WeatherCleared});
}

unsigned long WeatherService::now_ms() const
{
#if defined(ARDUINO)
    return millis();
#else
    using namespace std::chrono;
    return static_cast<unsigned long>(duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count());
#endif
}

SERVICE_REGISTER(WeatherService);