#include "services/WeatherService.h"

#include "EventBus.h"
#include "services/ConfigStoreService.h"
#include "services/FileSystemService.h"
#include "services/TimeService.h"
#include "services/WeatherLocationStore.h"
#include "services/WifiService.h"

#include <ArduinoJson.h>
#include <algorithm>
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <new>

#include "ui/UiTrace.h"

#if defined(ARDUINO)
#include <Arduino.h>
#include <HTTPClient.h>
#include <WiFiClient.h>
#include <WiFi.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#else
#include <chrono>
#include <mutex>
#include <thread>
#endif

namespace {

constexpr unsigned long WEATHER_REFRESH_MS = 10UL * 60UL * 1000UL;
constexpr double SYNODIC_MONTH_DAYS = 29.53058867;
constexpr double KNOWN_NEW_MOON_JD = 2451550.1;

float moon_phase_for_iso_date(const std::string &iso)
{
    int year = 0, month = 0, day = 0;
    if (std::sscanf(iso.c_str(), "%d-%d-%d", &year, &month, &day) != 3)
        return 0.0f;
    if (month <= 2) {
        --year;
        month += 12;
    }
    const double jd = std::floor(365.25 * (year + 4716)) +
                      std::floor(30.6001 * (month + 1)) + day - 1524.5;
    double phase = std::fmod((jd - KNOWN_NEW_MOON_JD) / SYNODIC_MONTH_DAYS, 1.0);
    if (phase < 0.0) phase += 1.0;
    return (float)phase;
}

struct WeatherFetchResult {
    bool ok = false;
    uint32_t cache_id = 0;
    std::string payload;
};

#if defined(ARDUINO)
QueueHandle_t weather_result_queue = nullptr;
#else
std::mutex weather_result_mutex;
WeatherFetchResult *weather_pending_result = nullptr;
#endif

std::string url_encode_timezone(const char *tz)
{
    if (!tz || !*tz)
        return "auto";

    std::string out;
    for (const char *p = tz; *p; ++p) {
        switch (*p) {
            case '/': out += "%2F"; break;
            case '+': out += "%2B"; break;
            case ' ': out += "%20"; break;
            default: out.push_back(*p); break;
        }
    }
    return out.empty() ? "auto" : out;
}

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

uint32_t weather_cache_id(double latitude, double longitude, bool metric)
{
    char key[64] = {};
    std::snprintf(key, sizeof(key), "%.6f,%.6f,%c", latitude, longitude, metric ? 'm' : 'i');
    uint32_t hash = 2166136261u;
    for (const unsigned char *p = (const unsigned char *)key; *p; ++p) {
        hash ^= *p;
        hash *= 16777619u;
    }
    return hash;
}

std::string weather_cache_path(double latitude, double longitude, bool metric)
{
    char path[48] = {};
    std::snprintf(path, sizeof(path), "/weather/%08lx.json",
                  (unsigned long)weather_cache_id(latitude, longitude, metric));
    return path;
}

}  // namespace

WeatherService &WeatherService::instance()
{
    static WeatherService inst;
    return inst;
}

bool WeatherService::begin()
{
#if defined(ARDUINO)
    if (!weather_result_queue)
        weather_result_queue = xQueueCreate(1, sizeof(WeatherFetchResult *));
#endif
    if (_wifi_connected_sub == 0)
        _wifi_connected_sub = EventBus::instance().subscribe(EventType::WifiConnected, on_wifi_connected, this);
    if (_wifi_disconnected_sub == 0)
        _wifi_disconnected_sub = EventBus::instance().subscribe(EventType::WifiDisconnected, on_wifi_disconnected, this);
    reload_from_config();
    if (_enabled && _configured && is_wifi_connected())
        fetch_and_update();

    return true;
}

void WeatherService::loop()
{
    process_async_result();
    if (!_enabled || !_configured || !is_wifi_connected())
        return;

    const unsigned long now = now_ms();
    if (_last_fetch_ms != 0 && (now - _last_fetch_ms) < WEATHER_REFRESH_MS)
        return;

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
    const bool new_metric = cfg.get_bool("metric", true);
    WeatherLocation location;
    if (!WeatherLocationStore::active(location)) {
        _configured = false;
        _has_utc_offset = false;
        clear_weather();
        return false;
    }
    const bool location_changed = !_configured || _latitude != location.latitude ||
                                  _longitude != location.longitude || _metric != new_metric;
    _metric = new_metric;
    _latitude = location.latitude;
    _longitude = location.longitude;
    _configured = true;

    if (location_changed) {
        _has_utc_offset = false;
        _last_fetch_ms = 0;
        std::string cached_payload;
        FileSystemService &files = FileSystemService::instance();
        files.begin();
        if (files.fs().read(weather_cache_path(_latitude, _longitude, _metric).c_str(), cached_payload))
            apply_payload(cached_payload);
        else
            clear_weather();
    }

    if (_enabled && is_wifi_connected())
        fetch_and_update();

    return true;
}

bool WeatherService::local_time(time_t utc, tm &result) const
{
    if (!_has_utc_offset) return false;
    // Convert independently of the process-wide TZ used by the device clock.
    const time_t shifted = utc + _utc_offset_seconds;
    return gmtime_r(&shifted, &result) != nullptr;
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

float WeatherService::humidity_value() const
{
    return _humidity;
}

float WeatherService::pressure_value() const
{
    return _pressure;
}

float WeatherService::precipitation_value() const
{
    return _precipitation;
}

float WeatherService::wind_speed_value() const
{
    return _wind_speed;
}

float WeatherService::wind_direction_degrees() const
{
    return _wind_direction;
}

float WeatherService::moon_illumination_percent() const
{
    return (float)((1.0 - std::cos(2.0 * 3.14159265358979323846 * _moon_phase)) * 50.0);
}

const char *WeatherService::moon_phase_name() const
{
    static const char *names[] = {
        "New moon", "Waxing crescent", "First quarter", "Waxing gibbous",
        "Full moon", "Waning gibbous", "Last quarter", "Waning crescent"};
    const int index = ((int)std::floor(_moon_phase * 8.0f + 0.5f)) & 7;
    return names[index];
}

int WeatherService::forecast_count() const
{
    return _forecast_count;
}

const WeatherForecastDay *WeatherService::forecast_days() const
{
    return _forecast;
}

int WeatherService::hourly_forecast_count() const
{
    return _hourly_forecast_count;
}

const WeatherForecastHour *WeatherService::forecast_hours() const
{
    return _hourly_forecast;
}

bool WeatherService::refresh_now()
{
    if (!_configured || !is_wifi_connected())
        return false;
    return fetch_and_update();
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
    // Weather is also displayed in the global header, so populate it at boot
    // even when the Weather app itself has not been opened yet.
    if (self && self->_configured)
        self->fetch_and_update();
}

void WeatherService::on_wifi_disconnected(const SystemEvent &event, void *user_data)
{
    (void)event;
    (void)user_data;
    // Keep the last successful reading visible while the network is unavailable.
}

bool WeatherService::is_wifi_connected() const
{
    return WifiService::instance().is_connected();
}

bool WeatherService::fetch_and_update()
{
    _last_fetch_ms = now_ms();
    return start_async_fetch();
}

bool WeatherService::apply_payload(std::string &payload)
{
    UI_TRACE("Weather", "fetch complete bytes=%u", (unsigned)payload.size());
    JsonDocument doc;
    DeserializationError json_error = deserializeJson(doc, payload.c_str(), payload.size());
    if (json_error != DeserializationError::Ok) {
        UI_TRACE("Weather", "parse failed error=%s", json_error.c_str());
        return false;
    }
    UI_TRACE("Weather", "parse complete");
    // Open-Meteo resolves this offset for timezone=auto, including current DST.
    // The original payload is cached per location, so it also works offline.
    const JsonVariantConst offset = doc["utc_offset_seconds"];
    _has_utc_offset = offset.is<int>() && offset.as<int>() >= -12 * 3600 &&
                      offset.as<int>() <= 14 * 3600;
    _utc_offset_seconds = _has_utc_offset ? offset.as<int>() : 0;


    JsonVariant current = doc["current"];
    if (!current.isNull()) {
        _temperature = current["temperature_2m"].as<float>();
        _weather_code = current["weather_code"].as<int>();
        _humidity = current["relative_humidity_2m"].as<float>();
        _pressure = current["pressure_msl"].as<float>();
        _precipitation = current["precipitation"].as<float>();
        _wind_speed = current["wind_speed_10m"].as<float>();
        _wind_direction = current["wind_direction_10m"].as<float>();
    } else {
        JsonVariant current_weather = doc["current_weather"];
        _temperature = current_weather["temperature"].as<float>();
        _weather_code = current_weather["weathercode"].as<int>();
        _humidity = 0.0f;
        _pressure = 0.0f;
        _precipitation = 0.0f;
        _wind_speed = current_weather["windspeed"].as<float>();
        _wind_direction = current_weather["winddirection"].as<float>();
    }

    _forecast_count = 0;
    _hourly_forecast_count = 0;
    _sunrise.clear();
    _sunset.clear();
    _moon_phase = 0.0f;
    for (int i = 0; i < FORECAST_DAYS; ++i)
        _forecast[i] = WeatherForecastDay{};
    for (int i = 0; i < FORECAST_HOURS; ++i)
        _hourly_forecast[i] = WeatherForecastHour{};

    JsonVariant daily = doc["daily"];
    if (!daily.isNull()) {
        JsonArray time_arr = daily["time"].as<JsonArray>();
        JsonArray code_arr = daily["weather_code"].as<JsonArray>();
        JsonArray max_arr = daily["temperature_2m_max"].as<JsonArray>();
        JsonArray min_arr = daily["temperature_2m_min"].as<JsonArray>();
        JsonArray sunrise_arr = daily["sunrise"].as<JsonArray>();
        JsonArray sunset_arr = daily["sunset"].as<JsonArray>();

        int available = (int)std::min({time_arr.size(), code_arr.size(), max_arr.size(), min_arr.size(), (size_t)FORECAST_DAYS});
        for (int i = 0; i < available; ++i) {
            _forecast[i].valid = true;
            _forecast[i].date = time_arr[i].as<std::string>();
            _forecast[i].weather_code = code_arr[i].as<int>();
            _forecast[i].temp_max = max_arr[i].as<float>();
            _forecast[i].temp_min = min_arr[i].as<float>();
        }
        _forecast_count = available;
        if (available > 0) {
            if (!sunrise_arr.isNull() && sunrise_arr.size() > 0)
                _sunrise = sunrise_arr[0].as<std::string>();
            if (!sunset_arr.isNull() && sunset_arr.size() > 0)
                _sunset = sunset_arr[0].as<std::string>();
            _moon_phase = moon_phase_for_iso_date(_forecast[0].date);
        }
    }

    JsonVariant hourly = doc["hourly"];
    JsonArray hourly_time = hourly["time"].as<JsonArray>();
    JsonArray hourly_code = hourly["weather_code"].as<JsonArray>();
    JsonArray hourly_temp = hourly["temperature_2m"].as<JsonArray>();
    const char *current_time_iso = doc["current"]["time"].as<const char *>();
    if (current_time_iso && !hourly_time.isNull() && !hourly_code.isNull() && !hourly_temp.isNull()) {
        int start_index = 0;
        size_t hourly_count = hourly_time.size();
        for (; start_index < (int)hourly_count; ++start_index) {
            const char *time_iso = hourly_time[start_index].as<const char *>();
            if (time_iso && std::strcmp(time_iso, current_time_iso) >= 0)
                break;
        }

        int available = (int)std::min({(int)hourly_count - start_index,
                                       (int)hourly_code.size() - start_index,
                                       (int)hourly_temp.size() - start_index,
                                       FORECAST_HOURS});
        if (available > 0) {
            for (int i = 0; i < available; ++i) {
                const int source_index = start_index + i;
                _hourly_forecast[i].valid = true;
                _hourly_forecast[i].time = hourly_time[source_index].as<std::string>();
                _hourly_forecast[i].weather_code = hourly_code[source_index].as<int>();
                _hourly_forecast[i].temperature = hourly_temp[source_index].as<float>();
            }
            _hourly_forecast_count = available;
        }
    }

    _has_weather = true;
    ++_data_revision;

    // Do not retain the response and ArduinoJson tree while subscribers rebuild
    // LVGL widgets. This peak matters on the original ESP32/Color Kit Grande.
    doc.clear();
    payload.clear();
    payload.shrink_to_fit();
    UI_TRACE("Weather", "update complete days=%d hours=%d", _forecast_count, _hourly_forecast_count);
    EventBus::instance().publish(SystemEvent{EventType::WeatherUpdated});
    UI_TRACE("Weather", "update published");
    return true;
}

bool WeatherService::start_async_fetch()
{
    if (_fetch_in_progress)
        return true;
#if defined(ARDUINO)
    if (!weather_result_queue)
        return false;
#endif
    _fetch_in_progress = true;
#if defined(ARDUINO)
    BaseType_t created = xTaskCreate(
        fetch_task_entry,
        "weather_http",
        8192,
        this,
        1,
        nullptr);
    if (created != pdPASS) {
        _fetch_in_progress = false;
        UI_TRACE("Weather", "failed to create HTTP task");
        return false;
    }
    UI_TRACE("Weather", "HTTP task started");
#else
    std::thread([this]() {
        WeatherFetchResult *result = new (std::nothrow) WeatherFetchResult{};
        if (result) {
            result->cache_id = weather_cache_id(_latitude, _longitude, _metric);
            result->ok = fetch_payload(result->payload);
        }
        std::lock_guard<std::mutex> lock(weather_result_mutex);
        delete weather_pending_result;
        weather_pending_result = result;
    }).detach();
#endif
    return true;
}

#if defined(ARDUINO)
void WeatherService::fetch_task_entry(void *user_data)
{
    WeatherService *self = static_cast<WeatherService *>(user_data);
    WeatherFetchResult *result = new (std::nothrow) WeatherFetchResult{};
    if (!self || !result) {
        if (self) self->_fetch_in_progress = false;
        delete result;
        vTaskDelete(nullptr);
        return;
    }

    result->cache_id = weather_cache_id(self->_latitude, self->_longitude, self->_metric);
    result->ok = self->fetch_payload(result->payload);
    if (xQueueSend(weather_result_queue, &result, 0) != pdTRUE) {
        delete result;
        self->_fetch_in_progress = false;
    }
    vTaskDelete(nullptr);
}
#endif

void WeatherService::process_async_result()
{
#if defined(ARDUINO)
    if (!weather_result_queue)
        return;

    WeatherFetchResult *result = nullptr;
    if (xQueueReceive(weather_result_queue, &result, 0) != pdTRUE)
        return;
#else
    WeatherFetchResult *result = nullptr;
    {
        std::lock_guard<std::mutex> lock(weather_result_mutex);
        result = weather_pending_result;
        weather_pending_result = nullptr;
    }
    if (!result) return;
#endif

    _fetch_in_progress = false;
    if (!result)
        return;

    const uint32_t current_id = weather_cache_id(_latitude, _longitude, _metric);
    if (result->ok && result->cache_id == current_id) {
        FileSystemService &files = FileSystemService::instance();
        files.begin();
        files.fs().mkdir_p("/weather");
        files.fs().write(weather_cache_path(_latitude, _longitude, _metric).c_str(), result->payload);
        apply_payload(result->payload);
    } else if (result->cache_id != current_id) {
        // A location changed during this request. Let loop() immediately start
        // the request for the newly active location.
        _last_fetch_ms = 0;
    }
    else
        UI_TRACE("Weather", "fetch failed");
    delete result;
}

bool WeatherService::fetch_payload(std::string &out_payload) const
{
#if !defined(ARDUINO)
    if (std::getenv("GADGET_OS_TUTORIAL_PATH")) {
        // A short deterministic delay keeps the real refresh animation visible.
        std::this_thread::sleep_for(std::chrono::milliseconds(900));
        FileSystemService &files = FileSystemService::instance();
        files.begin();
        return files.fs().read("/weather/51be3854.json", out_payload);
    }
#endif
    char url[768] = {0};
    const char *temp_unit = _metric ? "celsius" : "fahrenheit";
    const char *wind_unit = _metric ? "kmh" : "mph";
    const char *precip_unit = _metric ? "mm" : "inch";
    // Each saved location should use its own local forecast timestamps.
    const std::string tz_query = "auto";
    std::snprintf(url,
                  sizeof(url),
                  "http://api.open-meteo.com/v1/forecast?latitude=%.6f&longitude=%.6f&current=temperature_2m,relative_humidity_2m,precipitation,pressure_msl,wind_speed_10m,wind_direction_10m,weather_code&hourly=temperature_2m,weather_code&daily=weather_code,temperature_2m_max,temperature_2m_min,sunrise,sunset&forecast_days=%d&forecast_hours=%d&temperature_unit=%s&wind_speed_unit=%s&precipitation_unit=%s&timezone=%s",
                  _latitude,
                  _longitude,
                  FORECAST_DAYS,
                  FORECAST_HOURS,
                  temp_unit,
                  wind_unit,
                  precip_unit,
                  tz_query.c_str());

    UI_TRACE("Weather", "fetch_payload url=%s", url);
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
    std::string cmd = std::string("curl -s --max-time 8 \"") + url + "\"";

    FILE *pipe = popen(cmd.c_str(), "r");
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
    _forecast_count = 0;
    _hourly_forecast_count = 0;
    _sunrise.clear();
    _sunset.clear();
    _moon_phase = 0.0f;
    for (int i = 0; i < FORECAST_DAYS; ++i)
        _forecast[i] = WeatherForecastDay{};
    for (int i = 0; i < FORECAST_HOURS; ++i)
        _hourly_forecast[i] = WeatherForecastHour{};
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
