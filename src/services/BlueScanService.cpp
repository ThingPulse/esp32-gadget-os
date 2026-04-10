#include "services/BlueScanService.h"

#include "ServiceRegistry.h"

#include <algorithm>
#include <array>
#include <cstdio>

#if defined(ARDUINO) && defined(ESP_PLATFORM)
#include <Arduino.h>
#include <ArduinoJson.h>
#include <BLEAdvertisedDevice.h>
#include <BLEDevice.h>
#include <BLEScan.h>
#include <vector>

#if __has_include(<TheengsDecoder.h>)
#include <TheengsDecoder.h>
#define BLUE_SCAN_HAS_THEENGS 1
#elif __has_include(<decoder.h>)
#include <decoder.h>
#define BLUE_SCAN_HAS_THEENGS 1
#else
#define BLUE_SCAN_HAS_THEENGS 0
#endif
#else
#include <chrono>
#define BLUE_SCAN_HAS_THEENGS 0
#endif

namespace {

#if defined(ARDUINO) && defined(ESP_PLATFORM)
BLEScan *g_scan = nullptr;

#if BLUE_SCAN_HAS_THEENGS
TheengsDecoder g_decoder;
#endif

std::string hex_encode(const std::string &raw)
{
  static const char *HEX = "0123456789ABCDEF";
  std::string out;
  out.reserve(raw.size() * 2);
  for (unsigned char c : raw) {
    out.push_back(HEX[(c >> 4) & 0x0F]);
    out.push_back(HEX[c & 0x0F]);
  }
  return out;
}

#if BLUE_SCAN_HAS_THEENGS
template <typename DecoderT>
auto decoder_invoke(DecoderT &decoder, DynamicJsonDocument &doc, JsonObject &obj, int)
    -> decltype(decoder.decodeBLEJson(obj), bool())
{
  (void)doc;
  return decoder.decodeBLEJson(obj);
}

template <typename DecoderT>
auto decoder_invoke(DecoderT &decoder, DynamicJsonDocument &doc, JsonObject &obj, long)
    -> decltype(decoder.decodeBLEJson(doc), bool())
{
  (void)obj;
  return decoder.decodeBLEJson(doc);
}

bool decode_with_theengs(const std::string &addr,
                         int rssi,
                         const std::string &manufacturer,
                         const std::vector<std::pair<std::string, std::string>> &service_data,
                         std::string &decoded)
{
  DynamicJsonDocument doc(768);
  JsonObject payload = doc.to<JsonObject>();
  payload["id"] = addr;
  payload["rssi"] = rssi;

  if (!manufacturer.empty()) payload["manufacturerdata"] = hex_encode(manufacturer);

  if (!service_data.empty()) {
    JsonObject svc_obj = payload.createNestedObject("servicedatauuid");
    for (const auto &entry : service_data) svc_obj[entry.first] = hex_encode(entry.second);
  }

  const bool recognized = decoder_invoke(g_decoder, doc, payload, 0);
  if (!recognized) return false;

  char buf[64] = {0};
  bool has_metric = false;

  if (payload["tempc"].is<float>()) {
    std::snprintf(buf, sizeof(buf), "%.1f C", payload["tempc"].as<float>());
    decoded += std::string("Temp ") + buf;
    has_metric = true;
  }
  if (payload["hum"].is<float>()) {
    if (has_metric) decoded += " | ";
    std::snprintf(buf, sizeof(buf), "%.0f%%", payload["hum"].as<float>());
    decoded += std::string("Hum ") + buf;
    has_metric = true;
  }
  if (payload["batt"].is<int>()) {
    if (has_metric) decoded += " | ";
    std::snprintf(buf, sizeof(buf), "%d%%", payload["batt"].as<int>());
    decoded += std::string("Batt ") + buf;
    has_metric = true;
  }

  return has_metric;
}
#endif

class BlueScanCallbacks : public BLEAdvertisedDeviceCallbacks {
  void onResult(BLEAdvertisedDevice advertised) override
  {
    BlueScanService &svc = BlueScanService::instance();
    const std::string addr = advertised.getAddress().toString();
    const int slot = svc.find_slot_for_addr(addr);

    BlueScanDevice *devices = svc.mutable_devices();
    BlueScanDevice &row = devices[slot];
    row.valid = true;
    row.addr = addr;
    row.name = advertised.haveName() ? advertised.getName() : "Unnamed";
    row.rssi = advertised.getRSSI();
    row.last_seen_ms = millis();

#if BLUE_SCAN_HAS_THEENGS
    std::vector<std::pair<std::string, std::string>> service_data;
    for (int i = 0; i < advertised.getServiceDataCount(); ++i) {
      BLEUUID service_uuid = advertised.getServiceDataUUID(i);
      service_data.emplace_back(service_uuid.toString(), advertised.getServiceData(i));
    }

    std::string decoded;
    if (decode_with_theengs(addr,
                            row.rssi,
                            advertised.getManufacturerData(),
                            service_data,
                            decoded)) {
      row.decoded = decoded;
    } else {
      row.decoded = "No decoded metrics";
    }
#else
    row.decoded = "No decoded metrics";
#endif
  }
};

BlueScanCallbacks g_callbacks;
#endif

}  // namespace

BlueScanService &BlueScanService::instance()
{
  static BlueScanService inst;
  return inst;
}

bool BlueScanService::begin()
{
  clear_devices();
  _scanning = false;
#if BLUE_SCAN_HAS_THEENGS
  _decoder_available = true;
#else
  _decoder_available = false;
#endif
  return true;
}

void BlueScanService::loop()
{
#if !(defined(ARDUINO) && defined(ESP_PLATFORM))
  if (_scanning) simulate_devices();
#endif
}

void BlueScanService::start_scan()
{
  clear_devices();
  _scanning = true;

#if defined(ARDUINO) && defined(ESP_PLATFORM)
  if (!g_scan) {
    BLEDevice::init("");
    g_scan = BLEDevice::getScan();
    g_scan->setAdvertisedDeviceCallbacks(&g_callbacks, false);
    g_scan->setInterval(90);
    g_scan->setWindow(70);
    g_scan->setActiveScan(true);
  }

  if (g_scan) {
    g_scan->clearResults();
    g_scan->start(0, nullptr, false);
  }
#else
  _last_sim_ms = 0;
  _sim_tick = 0;
  simulate_devices();
#endif
}

void BlueScanService::stop_scan()
{
  _scanning = false;
#if defined(ARDUINO) && defined(ESP_PLATFORM)
  if (g_scan) g_scan->stop();
#endif
}

int BlueScanService::device_count() const
{
  int count = 0;
  for (const auto &d : _devices)
    if (d.valid) ++count;
  return count;
}

std::string BlueScanService::status_text() const
{
#if defined(ARDUINO) && defined(ESP_PLATFORM)
  if (!_scanning) return "Scanner stopped";
  if (_decoder_available) return "Scanning BLE + decoding telemetry...";
  return "Scanning BLE (decoder library unavailable)";
#else
  if (!_scanning) return "Simulator stopped";
  return "Native simulator: 2 sample BLE sensors";
#endif
}

void BlueScanService::clear_devices()
{
  for (auto &d : _devices) d = BlueScanDevice{};
}

int BlueScanService::find_slot_for_addr(const std::string &addr)
{
  for (int i = 0; i < MAX_DEVICES; ++i)
    if (_devices[i].valid && _devices[i].addr == addr) return i;

  for (int i = 0; i < MAX_DEVICES; ++i)
    if (!_devices[i].valid) return i;

  int oldest = 0;
  for (int i = 1; i < MAX_DEVICES; ++i)
    if (_devices[i].last_seen_ms < _devices[oldest].last_seen_ms) oldest = i;

  return oldest;
}

unsigned long BlueScanService::now_ms() const
{
#if defined(ARDUINO) && defined(ESP_PLATFORM)
  return millis();
#else
  using namespace std::chrono;
  return (unsigned long)duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
#endif
}

#if !(defined(ARDUINO) && defined(ESP_PLATFORM))
void BlueScanService::simulate_devices()
{
  const unsigned long now = now_ms();
  if (_last_sim_ms != 0 && (now - _last_sim_ms) < 2000UL) return;
  _last_sim_ms = now;
  ++_sim_tick;

  static const std::array<const char *, 2> names = {"ATC_Office", "LYWSD03_Lab"};
  static const std::array<const char *, 2> addrs = {"A4:C1:38:AA:10:01", "A4:C1:38:AA:10:02"};
  static const std::array<int, 2> base_rssi = {-58, -66};

  for (int i = 0; i < 2; ++i) {
    BlueScanDevice &d = _devices[i];
    d.valid = true;
    d.name = names[i];
    d.addr = addrs[i];
    d.rssi = base_rssi[i] + ((_sim_tick + i) % 5);
    d.last_seen_ms = now;

    const float temp = 20.5f + (float)((_sim_tick + i) % 6) * 0.3f;
    const float hum = 42.0f + (float)((_sim_tick + (i * 2)) % 7);
    const int batt = 87 - ((_sim_tick + i) % 4);

    char buf[96] = {0};
    std::snprintf(buf, sizeof(buf), "Temp %.1f C | Hum %.0f%% | Batt %d%%", temp, hum, batt);
    d.decoded = buf;
  }
}
#endif

SERVICE_REGISTER(BlueScanService);
