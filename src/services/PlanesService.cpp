#include "services/PlanesService.h"

#include "services/ConfigStoreService.h"
#include "services/WifiService.h"

#include <ArduinoJson.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>


#if defined(ARDUINO)
#include <Arduino.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClient.h>
#if defined(__has_include)
#if __has_include(<WiFiClientSecure.h>)
#include <WiFiClientSecure.h>
#define PLANES_HAS_SECURE_CLIENT 1
using PlanesSecureClient = WiFiClientSecure;
#elif __has_include(<NetworkClientSecure.h>)
#include <NetworkClientSecure.h>
#define PLANES_HAS_SECURE_CLIENT 1
using PlanesSecureClient = NetworkClientSecure;
#else
#define PLANES_HAS_SECURE_CLIENT 0
using PlanesSecureClient = WiFiClient;
#endif
#else
#define PLANES_HAS_SECURE_CLIENT 1
#include <WiFiClientSecure.h>
using PlanesSecureClient = WiFiClientSecure;
#endif
#else
#include <chrono>
#endif

namespace {

constexpr unsigned long PLANES_REFRESH_MS = 10UL * 1000UL;

static std::string trim_copy(const std::string &in)
{
    size_t start = 0;
    while (start < in.size() && (in[start] == ' ' || in[start] == '\t')) start++;

    size_t end = in.size();
    while (end > start && (in[end - 1] == ' ' || in[end - 1] == '\t')) end--;

    return in.substr(start, end - start);
}

}  // namespace

PlanesService &PlanesService::instance()
{
    static PlanesService inst;
    return inst;
}

bool PlanesService::begin()
{
    if (_wifi_connected_sub == 0)
        _wifi_connected_sub = EventBus::instance().subscribe(EventType::WifiConnected, on_wifi_connected, this);
    if (_wifi_disconnected_sub == 0)
        _wifi_disconnected_sub = EventBus::instance().subscribe(EventType::WifiDisconnected, on_wifi_disconnected, this);

    reload_from_config();
    if (_enabled && _configured && is_wifi_connected())
        fetch_and_update();

    return true;
}

void PlanesService::loop()
{
    if (!_enabled || !_configured || !is_wifi_connected())
        return;

    const unsigned long now = now_ms();
    if (_last_fetch_ms != 0 && (now - _last_fetch_ms) < PLANES_REFRESH_MS)
        return;

    fetch_and_update();
}

bool PlanesService::reload_from_config()
{
    ConfigStoreService &cfg = ConfigStoreService::instance();
    if (!cfg.begin()) {
        clear_planes();
        _configured = false;
        _last_error = "Config store unavailable";
        return false;
    }

    cfg.load("planes");
    std::string lat = cfg.get("center_latitude", "");
    std::string lon = cfg.get("center_longitude", "");
    std::string range = cfg.get("range_km", "");

    if (lat.empty() || lon.empty() || range.empty()) {
        _configured = false;
        clear_planes();
        _last_error = "Configure center and range";
        return false;
    }

    _center_lat = std::strtod(lat.c_str(), nullptr);
    _center_lon = std::strtod(lon.c_str(), nullptr);
    _range_km = std::strtod(range.c_str(), nullptr);

    if (!std::isfinite(_center_lat) || !std::isfinite(_center_lon) || !std::isfinite(_range_km) || _range_km <= 0.0) {
        _configured = false;
        clear_planes();
        _last_error = "Invalid center or range";
        return false;
    }

    _configured = true;
    if (_enabled && is_wifi_connected())
        fetch_and_update();

    return true;
}

bool PlanesService::refresh_now()
{
    if (!_configured || !is_wifi_connected())
        return false;
    return fetch_and_update();
}

void PlanesService::on_wifi_connected(const SystemEvent &event, void *user_data)
{
    (void)event;
    PlanesService *self = static_cast<PlanesService *>(user_data);
    if (self && self->_enabled && self->_configured)
        self->fetch_and_update();
}

void PlanesService::on_wifi_disconnected(const SystemEvent &event, void *user_data)
{
    (void)event;
    PlanesService *self = static_cast<PlanesService *>(user_data);
    if (self)
        self->clear_planes();
}

bool PlanesService::is_wifi_connected() const
{
    return WifiService::instance().is_connected();
}

bool PlanesService::fetch_and_update()
{
    _last_fetch_ms = now_ms();

    std::string payload;
    if (!fetch_payload(payload)) {
        clear_planes();
        if (_last_error.empty()) _last_error = "Network request failed";
        return false;
    }

    JsonDocument doc;
    if (deserializeJson(doc, payload.c_str(), payload.size()) != DeserializationError::Ok) {
        clear_planes();
        _last_error = "Invalid API response";
        return false;
    }

    _plane_count = 0;
    for (int i = 0; i < MAX_PLANES; ++i)
        _planes[i] = PlaneState{};

    JsonArray states = doc["states"].as<JsonArray>();
    if (!states.isNull()) {
        for (JsonVariant row_var : states) {
            if (_plane_count >= MAX_PLANES) break;

            JsonArray row = row_var.as<JsonArray>();
            if (row.isNull() || row.size() < 11) continue;

            if (row[5].isNull() || row[6].isNull()) continue;

            PlaneState &dst = _planes[_plane_count++];
            dst.valid = true;
            dst.icao24 = row[0].as<std::string>();
            dst.callsign = trim_copy(row[1].as<std::string>());
            if (dst.callsign.empty()) dst.callsign = "(no callsign)";
            dst.longitude = row[5].as<float>();
            dst.latitude = row[6].as<float>();
            dst.altitude_m = row[7].isNull() ? 0.0f : row[7].as<float>();
            dst.on_ground = row[8].isNull() ? false : row[8].as<bool>();
            dst.velocity_ms = row[9].isNull() ? 0.0f : row[9].as<float>();
        }
    }

    _has_data = true;
    _last_error.clear();
    return true;
}

bool PlanesService::fetch_payload(std::string &out_payload)
{
    const double lat_rad = _center_lat * 3.14159265358979323846 / 180.0;
    const double cos_lat = std::max(0.2, std::fabs(std::cos(lat_rad)));
    const double dlat = _range_km / 111.0;
    const double dlon = _range_km / (111.0 * cos_lat);

    const double lamin = std::max(-90.0, _center_lat - dlat);
    const double lamax = std::min(90.0, _center_lat + dlat);
    const double lomin = std::max(-180.0, _center_lon - dlon);
    const double lomax = std::min(180.0, _center_lon + dlon);

    char url[320] = {0};
    std::snprintf(url,
                  sizeof(url),
                  "https://opensky-network.org/api/states/all?lamin=%.6f&lomin=%.6f&lamax=%.6f&lomax=%.6f",
                  lamin,
                  lomin,
                  lamax,
                  lomax);

#if defined(ARDUINO)
    HTTPClient http;
    PlanesSecureClient client;
#if PLANES_HAS_SECURE_CLIENT
    client.setInsecure();
    if (!http.begin(client, url)) {
        _last_error = "HTTP begin failed (secure client)";
        return false;
    }
#else
    (void)client;
    _last_error = "HTTPS requires WiFiClientSecure/NetworkClientSecure; update ESP32 platform package";
    return false;
#endif

    http.setConnectTimeout(8000);
    http.setTimeout(12000);
    http.addHeader("Accept", "application/json");
    http.addHeader("User-Agent", "esp32-gadget-os/1.0");
    http.addHeader("Connection", "close");

    int code = http.GET();
    if (code != HTTP_CODE_OK) {
        std::string err = "HTTP error " + std::to_string(code);
        const String body = http.getString();
        if (!body.isEmpty()) {
            err += ": ";
            if (body.length() > 140) {
                err += body.substring(0, 140).c_str();
                err += "...";
            } else {
                err += body.c_str();
            }
        }
        _last_error = err;
        http.end();
        return false;
    }

    out_payload = http.getString().c_str();
    http.end();
    if (out_payload.empty()) {
        _last_error = "Empty response";
        return false;
    }
    return true;
#else
    char cmd[420] = {0};
    std::snprintf(cmd, sizeof(cmd), "curl -s --max-time 10 \"%s\"", url);

    FILE *pipe = popen(cmd, "r");
    if (!pipe) {
        _last_error = "Cannot run curl";
        return false;
    }

    out_payload.clear();
    char buffer[256];
    while (fgets(buffer, sizeof(buffer), pipe))
        out_payload += buffer;

    int rc = pclose(pipe);
    if (rc != 0 || out_payload.empty()) {
        _last_error = "Network request failed";
        return false;
    }
    return true;
#endif
}

void PlanesService::clear_planes()
{
    _has_data = false;
    _plane_count = 0;
    for (int i = 0; i < MAX_PLANES; ++i)
        _planes[i] = PlaneState{};
}

unsigned long PlanesService::now_ms() const
{
#if defined(ARDUINO)
    return millis();
#else
    using namespace std::chrono;
    return static_cast<unsigned long>(duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count());
#endif
}

SERVICE_REGISTER(PlanesService);