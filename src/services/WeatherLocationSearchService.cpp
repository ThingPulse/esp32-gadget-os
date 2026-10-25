#include "ui/I18n.h"
#include "services/WeatherLocationSearchService.h"

#include "ui/UiTrace.h"
#include "ui/AsciiFold.h"

#include <ArduinoJson.h>
#include <cctype>
#include <cstdio>
#include <new>
#include <cstdlib>

#if defined(ARDUINO)
#include <Arduino.h>
#include <HTTPClient.h>
#include <WiFiClient.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#else
#include <mutex>
#include <thread>
#endif

namespace {

struct SearchResult {
    bool ok = false;
    std::vector<WeatherLocation> locations;
    std::string error;
};

#if defined(ARDUINO)
QueueHandle_t result_queue = nullptr;
#else
std::mutex result_mutex;
SearchResult *pending_result = nullptr;
#endif

std::string url_encode(const std::string &text)
{
    static const char hex[] = "0123456789ABCDEF";
    std::string out;
    for (unsigned char ch : text) {
        if (std::isalnum(ch) || ch == '-' || ch == '_' || ch == '.') out.push_back((char)ch);
        else if (ch == ' ') out.push_back('+');
        else {
            out.push_back('%');
            out.push_back(hex[ch >> 4]);
            out.push_back(hex[ch & 15]);
        }
    }
    return out;
}

bool fetch_search(const std::string &query, SearchResult &result)
{
#if !defined(ARDUINO)
    // Documentation recordings must never depend on public-network latency.
    // Keep this opt-in so the regular native app still exercises the real API.
    if (std::getenv("GADGET_OS_TUTORIAL_PATH")) {
        WeatherLocation location;
        if (query.find("York") != std::string::npos || query.find("york") != std::string::npos) {
            location = {"New York", "New York", "United States", "America/New_York", 40.714272, -74.005966};
        } else {
            location = {"Zurich", "Canton of Zurich", "Switzerland", "Europe/Zurich", 47.366673, 8.550000};
        }
        result.locations.push_back(location);
        result.ok = true;
        return true;
    }
#endif
    // This public, non-sensitive lookup is available directly over HTTP. On
    // the original ESP32, avoiding TLS here saves the large temporary mbedTLS
    // handshake allocation that otherwise competes with the Settings UI.
    const std::string url = "http://geocoding-api.open-meteo.com/v1/search?name=" +
                            url_encode(query) + "&count=5&language=" + std::string(i18n::language_code()) + "&format=json";
    std::string payload;
#if defined(ARDUINO)
    WiFiClient client;
    HTTPClient http;
    if (!http.begin(client, url.c_str())) {
        result.error = i18n::tr("Could not start lookup");
        return false;
    }
    http.setConnectTimeout(5000);
    http.setTimeout(8000);
    http.addHeader("User-Agent", "esp32-gadget-os/1.0");
    const int code = http.GET();
    if (code != HTTP_CODE_OK) {
        result.error = i18n::tr("Lookup HTTP error");
        http.end();
        return false;
    }
    payload = http.getString().c_str();
    http.end();
#else
    const std::string command = "curl -sS -L --max-time 8 \"" + url + "\"";
    FILE *pipe = popen(command.c_str(), "r");
    if (!pipe) {
        result.error = i18n::tr("Could not start lookup");
        return false;
    }
    char buffer[512];
    while (fgets(buffer, sizeof(buffer), pipe)) payload += buffer;
    if (pclose(pipe) != 0 || payload.empty()) {
        result.error = i18n::tr("Lookup request failed");
        return false;
    }
#endif

    JsonDocument doc;
    if (deserializeJson(doc, payload.c_str(), payload.size()) != DeserializationError::Ok) {
        result.error = i18n::tr("Invalid lookup response");
        return false;
    }
    JsonArray items = doc["results"].as<JsonArray>();
    for (JsonObject item : items) {
        WeatherLocation location;
        location.name = ascii_fold_latin(item["name"].as<std::string>());
        location.region = ascii_fold_latin(item["admin1"].as<std::string>());
        location.country = ascii_fold_latin(item["country"].as<std::string>());
        location.timezone = item["timezone"].as<std::string>();
        location.latitude = item["latitude"].as<double>();
        location.longitude = item["longitude"].as<double>();
        if (!location.name.empty()) result.locations.push_back(location);
    }
    if (result.locations.empty()) {
        result.error = i18n::tr("No matching locations");
        return false;
    }
    result.ok = true;
    return true;
}

}  // namespace

WeatherLocationSearchService &WeatherLocationSearchService::instance()
{
    static WeatherLocationSearchService service;
    return service;
}

bool WeatherLocationSearchService::begin()
{
#if defined(ARDUINO)
    if (!result_queue) result_queue = xQueueCreate(1, sizeof(SearchResult *));
    return result_queue != nullptr;
#else
    return true;
#endif
}

void WeatherLocationSearchService::loop()
{
    process_result();
}

bool WeatherLocationSearchService::search(const std::string &query)
{
    if (_fetching || query.size() < 2) return false;
    _pending_query = query;
    _error.clear();
    _results.clear();
    return start_fetch(query);
}

bool WeatherLocationSearchService::start_fetch(const std::string &query)
{
    _fetching = true;
#if defined(ARDUINO)
    BaseType_t created = xTaskCreate(task_entry, "weather_places", 8192, this, 1, nullptr);
    if (created != pdPASS) {
        _fetching = false;
        _error = i18n::tr("Could not start lookup");
        return false;
    }
#else
    std::thread([this, query]() {
        SearchResult *result = new (std::nothrow) SearchResult{};
        if (result) fetch_search(query, *result);
        std::lock_guard<std::mutex> lock(result_mutex);
        delete pending_result;
        pending_result = result;
    }).detach();
#endif
    return true;
}

#if defined(ARDUINO)
void WeatherLocationSearchService::task_entry(void *user_data)
{
    WeatherLocationSearchService *self = static_cast<WeatherLocationSearchService *>(user_data);
    SearchResult *result = new (std::nothrow) SearchResult{};
    if (result) fetch_search(self->_pending_query, *result);
    if (!result || xQueueSend(result_queue, &result, 0) != pdTRUE) {
        delete result;
        self->_fetching = false;
    }
    vTaskDelete(nullptr);
}
#endif

void WeatherLocationSearchService::process_result()
{
    SearchResult *result = nullptr;
#if defined(ARDUINO)
    if (!result_queue || xQueueReceive(result_queue, &result, 0) != pdTRUE) return;
#else
    {
        std::lock_guard<std::mutex> lock(result_mutex);
        result = pending_result;
        pending_result = nullptr;
    }
    if (!result) return;
#endif
    _fetching = false;
    if (result->ok) _results.swap(result->locations);
    else _error = result->error.empty() ? i18n::tr("Location lookup failed") : result->error;
    ++_revision;
    UI_TRACE("Weather", "location search complete results=%u", (unsigned)_results.size());
    delete result;
}

SERVICE_REGISTER(WeatherLocationSearchService);
