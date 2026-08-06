#include "services/PlanesService.h"

#include "services/ConfigStoreService.h"
#include "services/WifiService.h"
#include "ui/UiTrace.h"

#include <ArduinoJson.h>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <new>


#if defined(ARDUINO)
#include <Arduino.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClient.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>
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
#include <mutex>
#include <thread>
#endif

// C++14 requires a definition when std::min binds this member by reference.
// Newer/local linkers may accept it implicitly, while GCC 13 correctly reports
// the missing ODR definition.
constexpr int PlanesService::MAX_PLANES;

namespace {

// Position providers are queried only while the Plane app is open. Fifteen
// seconds feels live without approaching the public APIs' per-second limits.
constexpr unsigned long PLANES_REFRESH_MS = 15UL * 1000UL;
constexpr unsigned long ROUTE_CACHE_OK_MS = 6UL * 60UL * 60UL * 1000UL;
constexpr unsigned long ROUTE_CACHE_MISS_MS = 30UL * 60UL * 1000UL;
constexpr int ROUTE_LOOKUPS_PER_CYCLE = 3;
constexpr size_t ROUTE_CACHE_MAX_ENTRIES = 512;

struct PlanesFetchResult {
    bool ok = false;
    PlaneState planes[PlanesService::MAX_PLANES]{};
    int count = 0;
    std::string provider;
    std::string error;
};

#if defined(ARDUINO)
QueueHandle_t planes_result_queue = nullptr;
#else
std::mutex planes_result_mutex;
PlanesFetchResult *planes_pending_result = nullptr;
#endif

static double deg_to_rad(double degrees)
{
    return degrees * 3.14159265358979323846 / 180.0;
}

static float distance_km(double lat1, double lon1, double lat2, double lon2)
{
    const double dlat = deg_to_rad(lat2 - lat1);
    const double dlon = deg_to_rad(lon2 - lon1);
    const double a = std::sin(dlat / 2.0) * std::sin(dlat / 2.0) +
                     std::cos(deg_to_rad(lat1)) * std::cos(deg_to_rad(lat2)) *
                     std::sin(dlon / 2.0) * std::sin(dlon / 2.0);
    return (float)(6371.0 * 2.0 * std::atan2(std::sqrt(a), std::sqrt(1.0 - a)));
}

static float bearing_deg(double lat1, double lon1, double lat2, double lon2)
{
    const double p1 = deg_to_rad(lat1);
    const double p2 = deg_to_rad(lat2);
    const double dl = deg_to_rad(lon2 - lon1);
    const double y = std::sin(dl) * std::cos(p2);
    const double x = std::cos(p1) * std::sin(p2) -
                     std::sin(p1) * std::cos(p2) * std::cos(dl);
    double value = std::atan2(y, x) * 180.0 / 3.14159265358979323846;
    if (value < 0.0) value += 360.0;
    return (float)value;
}

static std::string trim_copy(const std::string &in)
{
    size_t start = 0;
    while (start < in.size() && (in[start] == ' ' || in[start] == '\t')) start++;

    size_t end = in.size();
    while (end > start && (in[end - 1] == ' ' || in[end - 1] == '\t')) end--;

    return in.substr(start, end - start);
}

static std::string json_string_or_empty(JsonVariantConst value)
{
    const char *text = value.is<const char *>() ? value.as<const char *>() : nullptr;
    return text ? std::string(text) : std::string();
}

static std::string normalize_callsign_key(const std::string &callsign)
{
    std::string out;
    out.reserve(callsign.size());
    for (char ch : callsign) {
        if (ch == ' ' || ch == '\t') continue;
        out.push_back((char)std::toupper((unsigned char)ch));
    }
    return out;
}

static std::string airport_code_from_obj(const JsonVariantConst &airport)
{
    std::string iata = airport["iata_code"].as<std::string>();
    if (!iata.empty()) return iata;
    std::string icao = airport["icao_code"].as<std::string>();
    return icao;
}

// The compact LVGL fonts only contain ASCII. Fold the most common Latin UTF-8
// characters used in place names rather than showing LVGL's missing-glyph box.
static std::string airport_name_ascii(const std::string &text)
{
    std::string out;
    out.reserve(text.size());
    for (size_t i = 0; i < text.size();) {
        const unsigned char first = (unsigned char)text[i];
        if (first < 0x80) {
            out.push_back((char)first);
            ++i;
            continue;
        }

        uint32_t cp = 0;
        size_t count = 0;
        if ((first & 0xe0) == 0xc0 && i + 1 < text.size()) {
            cp = ((uint32_t)(first & 0x1f) << 6) |
                 ((uint32_t)text[i + 1] & 0x3f);
            count = 2;
        } else if ((first & 0xf0) == 0xe0 && i + 2 < text.size()) {
            cp = ((uint32_t)(first & 0x0f) << 12) |
                 (((uint32_t)text[i + 1] & 0x3f) << 6) |
                 ((uint32_t)text[i + 2] & 0x3f);
            count = 3;
        } else {
            ++i;
            continue;
        }
        i += count;

        if ((cp >= 0x00c0 && cp <= 0x00c5) || cp == 0x0100 || cp == 0x0102 || cp == 0x0104) out += 'A';
        else if ((cp >= 0x00e0 && cp <= 0x00e5) || cp == 0x0101 || cp == 0x0103 || cp == 0x0105) out += 'a';
        else if (cp == 0x00c7 || cp == 0x0106 || cp == 0x010c) out += 'C';
        else if (cp == 0x00e7 || cp == 0x0107 || cp == 0x010d) out += 'c';
        else if (cp >= 0x00c8 && cp <= 0x00cb) out += 'E';
        else if (cp >= 0x00e8 && cp <= 0x00eb) out += 'e';
        else if (cp >= 0x00cc && cp <= 0x00cf) out += 'I';
        else if (cp >= 0x00ec && cp <= 0x00ef) out += 'i';
        else if (cp == 0x00d1) out += 'N';
        else if (cp == 0x00f1) out += 'n';
        else if ((cp >= 0x00d2 && cp <= 0x00d6) || cp == 0x00d8) out += 'O';
        else if ((cp >= 0x00f2 && cp <= 0x00f6) || cp == 0x00f8) out += 'o';
        else if (cp >= 0x00d9 && cp <= 0x00dc) out += 'U';
        else if (cp >= 0x00f9 && cp <= 0x00fc) out += 'u';
        else if (cp == 0x00dd || cp == 0x0178) out += 'Y';
        else if (cp == 0x00fd || cp == 0x00ff) out += 'y';
        else if (cp == 0x00df) out += "ss";
        else if (cp == 0x0141) out += 'L';
        else if (cp == 0x0142) out += 'l';
        else if (cp == 0x2013 || cp == 0x2014) out += '-';
        else if (cp == 0x2018 || cp == 0x2019) out += '\'';
    }
    return out;
}

static std::string airport_display_name_from_obj(const JsonVariantConst &airport)
{
    std::string municipality = json_string_or_empty(airport["municipality"]);
    if (!municipality.empty()) return airport_name_ascii(municipality);
    return airport_name_ascii(json_string_or_empty(airport["name"]));
}

#if !defined(ARDUINO)
static std::string snippet_copy(const std::string &s, size_t max_len = 240)
{
    if (s.size() <= max_len) return s;
    return s.substr(0, max_len) + "...";
}

static bool emulator_http_get_logged(const char *tag,
                                     const char *url,
                                     int timeout_seconds,
                                     std::string &out_body,
                                     long &out_http_code,
                                     int &out_exit_code,
                                     long *out_rate_limit_remaining = nullptr)
{
    out_body.clear();
    out_http_code = 0;
    out_exit_code = -1;

    char cmd[1024] = {0};
    if (out_rate_limit_remaining) {
        std::snprintf(cmd,
                      sizeof(cmd),
                      "curl -sS -L --max-time %d"
                      " -w '\\n__COPILOT_HTTP_CODE:%%{http_code}\\n__COPILOT_RATE_LIMIT:%%{header{x-rate-limit-remaining}}\\n'"
                      " \"%s\" 2>&1",
                      timeout_seconds,
                      url);
    } else {
        std::snprintf(cmd,
                      sizeof(cmd),
                      "curl -sS -L --max-time %d -w '\\n__COPILOT_HTTP_CODE:%%{http_code}\\n' \"%s\" 2>&1",
                      timeout_seconds,
                      url);
    }

    UI_TRACE(tag, "REQ %s", url);

    FILE *pipe = popen(cmd, "r");
    if (!pipe) {
        UI_TRACE(tag, "RESP popen failed");
        return false;
    }

    char buffer[256];
    while (fgets(buffer, sizeof(buffer), pipe))
        out_body += buffer;

    out_exit_code = pclose(pipe);

    const std::string marker = "\n__COPILOT_HTTP_CODE:";
    size_t marker_pos = out_body.rfind(marker);
    if (marker_pos != std::string::npos) {
        size_t code_start = marker_pos + marker.size();
        size_t code_end = out_body.find('\n', code_start);
        std::string code_str = out_body.substr(code_start, code_end - code_start);
        out_http_code = std::strtol(code_str.c_str(), nullptr, 10);

        if (out_rate_limit_remaining && code_end != std::string::npos) {
            const std::string rl_marker = "__COPILOT_RATE_LIMIT:";
            size_t rl_pos = out_body.find(rl_marker, code_end + 1);
            if (rl_pos != std::string::npos) {
                size_t rl_start = rl_pos + rl_marker.size();
                size_t rl_end = out_body.find('\n', rl_start);
                std::string rl_str = out_body.substr(rl_start,
                    rl_end == std::string::npos ? std::string::npos : rl_end - rl_start);
                rl_str = trim_copy(rl_str);
                *out_rate_limit_remaining = rl_str.empty() ? -1L : std::strtol(rl_str.c_str(), nullptr, 10);
            } else {
                *out_rate_limit_remaining = -1L;
            }
        }

        out_body.erase(marker_pos);
    }

    UI_TRACE(tag,
             "RESP code=%ld exit=%d bytes=%zu body=%s",
             out_http_code,
             out_exit_code,
             out_body.size(),
             snippet_copy(out_body).c_str());

    return out_exit_code == 0 && !out_body.empty();
}
#endif

}  // namespace

PlanesService &PlanesService::instance()
{
#if defined(ARDUINO)
    // PlaneState and the route cache make this singleton too large for the
    // ESP32's statically linked DRAM segment. Allocate it from the runtime
    // heap instead; the service still has process lifetime through this
    // function-local pointer.
    static PlanesService *inst = new PlanesService;
    return *inst;
#else
    static PlanesService inst;
    return inst;
#endif
}

bool PlanesService::begin()
{
#if defined(ARDUINO)
    if (!planes_result_queue)
        planes_result_queue = xQueueCreate(1, sizeof(PlanesFetchResult *));
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

void PlanesService::loop()
{
    process_async_result();
    if (!_enabled || !_configured || !is_wifi_connected())
        return;

    const unsigned long now = now_ms();
    if (_last_fetch_ms == 0 || (now - _last_fetch_ms) >= PLANES_REFRESH_MS)
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
    _metric = cfg.get_bool("metric", true);
    _hide_ground = cfg.get_bool("hide_ground", false);

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
    (void)user_data;
    // Keep the last successful snapshot visible during transient disconnects.
}

bool PlanesService::is_wifi_connected() const
{
    return WifiService::instance().is_connected();
}

bool PlanesService::fetch_and_update()
{
    _last_fetch_ms = now_ms();
    return start_async_fetch();
}

bool PlanesService::parse_payload(const std::string &payload,
                                  PlaneState *out_planes,
                                  int &out_count,
                                  std::string &out_error)
{
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, payload.c_str(), payload.size());
    if (err != DeserializationError::Ok) {
#if !defined(ARDUINO)
        UI_TRACE("Planes", "JSON parse failed err=%s body=%s", err.c_str(), snippet_copy(payload).c_str());
#endif
        out_error = "Invalid API response";
        return false;
    }

    out_count = 0;
    auto retain_nearest = [&](PlaneState &candidate) {
        if (!candidate.valid || candidate.distance_km > _range_km)
            return;
        if (_hide_ground && candidate.on_ground)
            return;

        if (out_count < MAX_PLANES) {
            out_planes[out_count++] = candidate;
            return;
        }

        int farthest = 0;
        for (int i = 1; i < out_count; ++i) {
            if (out_planes[i].distance_km > out_planes[farthest].distance_km)
                farthest = i;
        }
        if (candidate.distance_km < out_planes[farthest].distance_km)
            out_planes[farthest] = candidate;
    };

    JsonArray aircraft = doc["ac"].as<JsonArray>();
    if (!aircraft.isNull()) {
        for (JsonVariant item : aircraft) {
            JsonObjectConst ac = item.as<JsonObjectConst>();
            if (ac.isNull() || ac["lat"].isNull() || ac["lon"].isNull())
                continue;

            PlaneState candidate;
            candidate.valid = true;
            candidate.icao24 = json_string_or_empty(ac["hex"]);
            candidate.callsign = trim_copy(json_string_or_empty(ac["flight"]));
            if (candidate.callsign.empty()) candidate.callsign = candidate.icao24;
            candidate.registration = json_string_or_empty(ac["r"]);
            candidate.aircraft_type = json_string_or_empty(ac["t"]);
            candidate.aircraft_description = json_string_or_empty(ac["desc"]);
            candidate.category = json_string_or_empty(ac["category"]);
            candidate.squawk = json_string_or_empty(ac["squawk"]);
            candidate.latitude = ac["lat"].as<float>();
            candidate.longitude = ac["lon"].as<float>();

            JsonVariantConst altitude = ac["alt_baro"];
            if (altitude.is<const char *>() && std::string(altitude.as<const char *>()) == "ground") {
                candidate.on_ground = true;
            } else if (altitude.is<float>() || altitude.is<int>()) {
                candidate.altitude_m = altitude.as<float>() / 3.28084f;
            }
            candidate.velocity_ms = ac["gs"].isNull() ? 0.0f : ac["gs"].as<float>() / 1.943844f;
            candidate.heading_valid = !ac["track"].isNull();
            candidate.heading_deg = candidate.heading_valid ? ac["track"].as<float>() : 0.0f;
            candidate.vertical_rate_ms = ac["baro_rate"].isNull()
                ? 0.0f : ac["baro_rate"].as<float>() * 0.00508f;
            candidate.military = (ac["dbFlags"].as<int>() & 1) != 0;
            candidate.distance_km = distance_km(_center_lat, _center_lon,
                                                 candidate.latitude, candidate.longitude);
            candidate.bearing_deg = bearing_deg(_center_lat, _center_lon,
                                                 candidate.latitude, candidate.longitude);
            retain_nearest(candidate);
        }
    } else {
        JsonArray states = doc["states"].as<JsonArray>();
        if (states.isNull()) {
            out_error = "Provider returned no aircraft list";
            return false;
        }

        for (JsonVariant row_value : states) {
            JsonArray row = row_value.as<JsonArray>();
            if (row.isNull() || row.size() < 11 || row[5].isNull() || row[6].isNull())
                continue;

            PlaneState candidate;
            candidate.valid = true;
            candidate.icao24 = json_string_or_empty(row[0]);
            candidate.callsign = trim_copy(json_string_or_empty(row[1]));
            if (candidate.callsign.empty()) candidate.callsign = candidate.icao24;
            candidate.origin_country = json_string_or_empty(row[2]);
            candidate.longitude = row[5].as<float>();
            candidate.latitude = row[6].as<float>();
            candidate.altitude_m = row[7].isNull() ? 0.0f : row[7].as<float>();
            candidate.on_ground = row[8].isNull() ? false : row[8].as<bool>();
            candidate.velocity_ms = row[9].isNull() ? 0.0f : row[9].as<float>();
            candidate.heading_valid = !row[10].isNull();
            candidate.heading_deg = candidate.heading_valid ? row[10].as<float>() : 0.0f;
            candidate.vertical_rate_ms = row.size() > 11 && !row[11].isNull() ? row[11].as<float>() : 0.0f;
            candidate.squawk = row.size() > 14 ? json_string_or_empty(row[14]) : "";
            if (row.size() > 17 && !row[17].isNull())
                candidate.category = std::to_string(row[17].as<int>());
            candidate.distance_km = distance_km(_center_lat, _center_lon,
                                                 candidate.latitude, candidate.longitude);
            candidate.bearing_deg = bearing_deg(_center_lat, _center_lon,
                                                 candidate.latitude, candidate.longitude);
            retain_nearest(candidate);
        }
    }

    std::sort(out_planes, out_planes + out_count,
              [](const PlaneState &a, const PlaneState &b) {
                  return a.distance_km < b.distance_km;
              });
    return true;
}

void PlanesService::enrich_routes(PlaneState *planes, int count)
{
#if !defined(ARDUINO)
    if (std::getenv("GADGET_OS_TUTORIAL_PATH")) {
        static const char *routes[] = {"ZRH->JFK", "DUB->ZRH", "FRA->ZRH"};
        for (int i = 0; i < count; ++i) planes[i].route = routes[i % 3];
        return;
    }
#endif
    int network_budget = ROUTE_LOOKUPS_PER_CYCLE;
    for (int i = 0; i < count; ++i) {
        std::string origin_name;
        std::string destination_name;
        planes[i].route = route_for_callsign(planes[i].callsign, network_budget,
                                             origin_name, destination_name);
    }
}

bool PlanesService::route_airport_names(const std::string &callsign, std::string &origin,
                                        std::string &destination) const
{
    origin.clear();
    destination.clear();
    const std::string key = normalize_callsign_key(callsign);
    auto it = _route_cache.find(key);
    if (it == _route_cache.end()) return false;
    origin = it->second.origin_name;
    destination = it->second.destination_name;
    return !origin.empty() || !destination.empty();
}

void PlanesService::publish_result(PlaneState *planes, int count, const std::string &provider)
{
    // Avoid ODR-using the C++14 static constexpr through std::min's
    // reference parameters (GCC 13's native linker requires a definition).
    _plane_count = count < MAX_PLANES ? count : MAX_PLANES;
    for (int i = 0; i < MAX_PLANES; ++i)
        _planes[i] = i < _plane_count ? planes[i] : PlaneState{};
    _has_data = true;
    _provider_name = provider;
    _last_error.clear();
    ++_data_revision;
    UI_TRACE("Planes", "update complete provider=%s planes=%d", provider.c_str(), _plane_count);
}

std::string PlanesService::route_for_callsign(const std::string &callsign_raw, int &network_budget,
                                              std::string &out_origin_name,
                                              std::string &out_destination_name)
{
    out_origin_name.clear();
    out_destination_name.clear();
    const std::string key = normalize_callsign_key(callsign_raw);
    if (key.empty() || key == "(NOCALLSIGN)") return "N/A";

    const unsigned long now = now_ms();
    auto it = _route_cache.find(key);
    if (it != _route_cache.end() && it->second.expires_at_ms > now) {
        out_origin_name = it->second.origin_name;
        out_destination_name = it->second.destination_name;
        if (it->second.route.empty()) return "N/A";
        return it->second.route;
    }

    if (network_budget <= 0) return "";
    network_budget--;

    std::string route;
    if (fetch_route_for_callsign(key, route, out_origin_name, out_destination_name)) {
        _route_cache[key] = RouteCacheEntry{route, out_origin_name, out_destination_name,
                                            now + ROUTE_CACHE_OK_MS};
    } else {
        _route_cache[key] = RouteCacheEntry{"", "", "", now + ROUTE_CACHE_MISS_MS};
        route = "N/A";
    }

    if (_route_cache.size() > ROUTE_CACHE_MAX_ENTRIES)
        _route_cache.clear();

    return route;
}

bool PlanesService::fetch_route_for_callsign(const std::string &callsign_key, std::string &out_route,
                                             std::string &out_origin_name,
                                             std::string &out_destination_name)
{
    out_route.clear();
    out_origin_name.clear();
    out_destination_name.clear();
    std::string payload;

    char url[256] = {0};
    std::snprintf(url, sizeof(url), "https://api.adsbdb.com/v0/callsign/%s", callsign_key.c_str());

#if defined(ARDUINO)
    HTTPClient http;
    PlanesSecureClient client;
#if PLANES_HAS_SECURE_CLIENT
    client.setInsecure();
    if (!http.begin(client, url)) {
        return false;
    }
#else
    (void)client;
    return false;
#endif

    http.setConnectTimeout(4000);
    http.setTimeout(6000);
    http.addHeader("Accept", "application/json");
    http.addHeader("User-Agent", "esp32-gadget-os/1.0");
    http.addHeader("Connection", "close");

    int code = http.GET();
    if (code != HTTP_CODE_OK) {
        http.end();
        return false;
    }

    payload = http.getString().c_str();
    http.end();
    if (payload.empty()) return false;
#else
    long http_code = 0;
    int exit_code = 0;
    if (!emulator_http_get_logged("PlanesADSB", url, 8, payload, http_code, exit_code))
        return false;
    if (http_code != 200)
        return false;
#endif

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, payload.c_str(), payload.size());
    if (err != DeserializationError::Ok) {
#if !defined(ARDUINO)
        UI_TRACE("PlanesADSB", "JSON parse failed err=%s body=%s", err.c_str(), snippet_copy(payload).c_str());
#endif
        return false;
    }

    JsonVariantConst flightroute = doc["response"]["flightroute"];
    if (flightroute.isNull()) return false;

    std::string origin = airport_code_from_obj(flightroute["origin"]);
    std::string destination = airport_code_from_obj(flightroute["destination"]);
    if (origin.empty() || destination.empty()) return false;

    out_route = origin + "->" + destination;
    out_origin_name = airport_display_name_from_obj(flightroute["origin"]);
    out_destination_name = airport_display_name_from_obj(flightroute["destination"]);
    return true;
}

bool PlanesService::start_async_fetch()
{
    if (_fetch_in_progress)
        return true;
#if defined(ARDUINO)
    if (!planes_result_queue) {
        _last_error = "Result queue unavailable";
        return false;
    }

    _fetch_in_progress = true;
    BaseType_t created = xTaskCreate(fetch_task_entry, "planes_http", 12288, this, 1, nullptr);
    if (created != pdPASS) {
        _fetch_in_progress = false;
        _last_error = "Could not start Plane request";
        UI_TRACE("Planes", "failed to create HTTP task");
        return false;
    }
    UI_TRACE("Planes", "HTTP task started");
    return true;
#else
    _fetch_in_progress = true;
    try {
        std::thread([this]() {
            PlanesFetchResult *result = new (std::nothrow) PlanesFetchResult{};
            if (!result) {
                _fetch_in_progress = false;
                return;
            }
            std::string payload;
            result->ok = fetch_payload(payload, result->provider, result->error);
            if (result->ok)
                result->ok = parse_payload(payload, result->planes, result->count, result->error);
            if (result->ok)
                enrich_routes(result->planes, result->count);

            std::lock_guard<std::mutex> lock(planes_result_mutex);
            delete planes_pending_result;
            planes_pending_result = result;
        }).detach();
    } catch (...) {
        _fetch_in_progress = false;
        _last_error = "Could not start Plane request";
        return false;
    }
    UI_TRACE("Planes", "native HTTP thread started");
    return true;
#endif
}

#if defined(ARDUINO)
void PlanesService::fetch_task_entry(void *user_data)
{
    PlanesService *self = static_cast<PlanesService *>(user_data);
    PlanesFetchResult *result = new (std::nothrow) PlanesFetchResult{};
    if (!self || !result) {
        if (self) self->_fetch_in_progress = false;
        delete result;
        vTaskDelete(nullptr);
        return;
    }

    std::string payload;
    result->ok = self->fetch_payload(payload, result->provider, result->error);
    if (result->ok)
        result->ok = self->parse_payload(payload, result->planes, result->count, result->error);
    if (result->ok)
        self->enrich_routes(result->planes, result->count);

    payload.clear();
    payload.shrink_to_fit();
    if (xQueueSend(planes_result_queue, &result, 0) != pdTRUE) {
        delete result;
        self->_fetch_in_progress = false;
    }
    vTaskDelete(nullptr);
}
#endif

void PlanesService::process_async_result()
{
#if defined(ARDUINO)
    if (!planes_result_queue)
        return;

    PlanesFetchResult *result = nullptr;
    if (xQueueReceive(planes_result_queue, &result, 0) != pdTRUE)
        return;
#else
    PlanesFetchResult *result = nullptr;
    {
        std::lock_guard<std::mutex> lock(planes_result_mutex);
        result = planes_pending_result;
        planes_pending_result = nullptr;
    }
    if (!result)
        return;
#endif

    _fetch_in_progress = false;
    if (!result)
        return;

    if (result->ok) {
        publish_result(result->planes, result->count, result->provider);
    } else {
        _last_error = result->error.empty() ? "All aircraft providers failed" : result->error;
        UI_TRACE("Planes", "fetch failed error=%s", _last_error.c_str());
    }
    delete result;
}

bool PlanesService::fetch_payload(std::string &out_payload,
                                  std::string &out_provider,
                                  std::string &out_error)
{
#if !defined(ARDUINO)
    if (std::getenv("GADGET_OS_TUTORIAL_PATH")) {
        std::this_thread::sleep_for(std::chrono::milliseconds(900));
        out_provider = "Tutorial fixture";
        out_error.clear();
        out_payload = R"json({"ac":[
          {"hex":"4b1805","flight":"SWR12A","lat":47.48,"lon":8.62,"alt_baro":12300,"gs":310,"track":245,"baro_rate":-640,"r":"HB-JBA","t":"BCS3"},
          {"hex":"4ca9d2","flight":"RYR8ZH","lat":47.29,"lon":8.38,"alt_baro":18400,"gs":355,"track":62,"baro_rate":832,"r":"EI-EVH","t":"B738"},
          {"hex":"3c65a1","flight":"DLH5CP","lat":47.39,"lon":8.75,"alt_baro":9600,"gs":270,"track":278,"baro_rate":0,"r":"D-AINA","t":"A20N"}
        ]})json";
        return true;
    }
#endif
    const int radius_nm = std::max(1, std::min(250, (int)std::ceil(_range_km / 1.852)));
    const double lat_rad = _center_lat * 3.14159265358979323846 / 180.0;
    const double cos_lat = std::max(0.2, std::fabs(std::cos(lat_rad)));
    const double dlat = _range_km / 111.0;
    const double dlon = _range_km / (111.0 * cos_lat);

    const double lamin = std::max(-90.0, _center_lat - dlat);
    const double lamax = std::min(90.0, _center_lat + dlat);
    const double lomin = std::max(-180.0, _center_lon - dlon);
    const double lomax = std::min(180.0, _center_lon + dlon);

    char urls[4][320]{};
#if defined(ARDUINO) && defined(ESP_PLATFORM) && !defined(COLOR_KIT_PRO)
    // The original Color Kit cannot reliably reserve enough contiguous
    // internal RAM for an mbedTLS handshake while the Plane UI is resident.
    // adsb.lol also serves this anonymous, read-only endpoint over HTTP.
    std::snprintf(urls[0], sizeof(urls[0]),
                  "http://api.adsb.lol/v2/point/%.4f/%.4f/%d",
                  _center_lat, _center_lon, radius_nm);
#else
    std::snprintf(urls[0], sizeof(urls[0]),
                  "https://api.adsb.lol/v2/point/%.4f/%.4f/%d",
                  _center_lat, _center_lon, radius_nm);
#endif
    std::snprintf(urls[1], sizeof(urls[1]),
                  "https://api.airplanes.live/v2/point/%.4f/%.4f/%d",
                  _center_lat, _center_lon, radius_nm);
    std::snprintf(urls[2], sizeof(urls[2]),
                  "https://opendata.adsb.fi/api/v2/lat/%.4f/lon/%.4f/dist/%d",
                  _center_lat, _center_lon, radius_nm);
    std::snprintf(urls[3], sizeof(urls[3]),
                  "https://opensky-network.org/api/states/all?lamin=%.6f&lomin=%.6f&lamax=%.6f&lomax=%.6f",
                  lamin, lomin, lamax, lomax);
    static const char *providers[] = {"adsb.lol", "airplanes.live", "adsb.fi", "OpenSky"};

    out_payload.clear();
    out_provider.clear();
    out_error.clear();

#if defined(ARDUINO)
#if PLANES_HAS_SECURE_CLIENT
    for (int i = 0; i < 4; ++i) {
        HTTPClient http;
#if defined(ESP_PLATFORM) && !defined(COLOR_KIT_PRO)
        WiFiClient plain_client;
        if (i == 0) {
            if (!http.begin(plain_client, urls[i])) continue;
        } else
#endif
        {
        PlanesSecureClient client;
        client.setInsecure();
        if (!http.begin(client, urls[i])) continue;

        http.setConnectTimeout(5000);
        http.setTimeout(10000);
        http.addHeader("Accept", "application/json");
        http.addHeader("User-Agent", "esp32-gadget-os/1.0");
        http.addHeader("Connection", "close");

        UI_TRACE("Planes", "request provider=%s url=%s", providers[i], urls[i]);
        int code = http.GET();
        if (code == HTTP_CODE_OK) {
            out_payload = http.getString().c_str();
            http.end();
            if (!out_payload.empty()) {
                out_provider = providers[i];
                return true;
            }
        } else {
            UI_TRACE("Planes", "provider=%s HTTP code=%d", providers[i], code);
        }
        http.end();
        continue;
        }

#if defined(ESP_PLATFORM) && !defined(COLOR_KIT_PRO)
        http.setConnectTimeout(5000);
        http.setTimeout(10000);
        http.addHeader("Accept", "application/json");
        http.addHeader("User-Agent", "esp32-gadget-os/1.0");
        http.addHeader("Connection", "close");
        UI_TRACE("Planes", "request provider=%s url=%s", providers[i], urls[i]);
        int code = http.GET();
        if (code == HTTP_CODE_OK) {
            out_payload = http.getString().c_str();
            http.end();
            if (!out_payload.empty()) {
                out_provider = providers[i];
                return true;
            }
        } else {
            UI_TRACE("Planes", "provider=%s HTTP code=%d", providers[i], code);
        }
        http.end();
#endif
    }
    out_error = "All aircraft providers failed";
    return false;
#else
    out_error = "HTTPS client unavailable";
    return false;
#endif
#else
    for (int i = 0; i < 4; ++i) {
        long http_code = 0;
        int exit_code = 0;
        if (emulator_http_get_logged("Planes", urls[i], 10,
                                     out_payload, http_code, exit_code) &&
            http_code == 200) {
            out_provider = providers[i];
            return true;
        }
    }
    out_error = "All aircraft providers failed";
    return false;
#endif
}

void PlanesService::clear_planes()
{
    _has_data = false;
    _plane_count = 0;
    _provider_name.clear();
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
