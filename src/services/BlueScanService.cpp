#include "services/BlueScanService.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <string>
#include <vector>

#if defined(ARDUINO)
#include <Arduino.h>
#else
#include <chrono>
#endif

#if defined(ARDUINO) && defined(ESP_PLATFORM)
#include <ArduinoJson.h>
#include <BLEAdvertisedDevice.h>
#include <BLEDevice.h>
#include <BLEScan.h>
#include <freertos/queue.h>

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
#define BLUE_SCAN_HAS_THEENGS 0
#endif

namespace {

#if defined(ARDUINO) && defined(ESP_PLATFORM)
static BLEScan *g_scan = nullptr;

// Raw advertisement data captured entirely from the BTC_TASK callback.
// All fields are fixed-size to avoid heap allocation inside the BLE callback.
struct RawAdv {
    char addr[20];      // "xx:xx:xx:xx:xx:xx\0"
    char name[33];
    int rssi;
    uint8_t mfr[64];   // raw manufacturer data bytes
    uint8_t mfr_len;
    struct SvcEntry {
        char uuid[48];  // "0000xxxx-0000-1000-8000-00805f9b34fb\0"
        uint8_t data[32];
        uint8_t data_len;
    } svc[3];
    uint8_t svc_count;
};
static constexpr int ADV_QUEUE_DEPTH = 16;

#if BLUE_SCAN_HAS_THEENGS
static TheengsDecoder g_decoder;
#endif

static std::string hex_encode(const std::string &raw)
{
    static const char *kHexDigits = "0123456789ABCDEF";
    std::string out;
    out.reserve(raw.size() * 2);
    for (unsigned char c : raw) {
        out.push_back(kHexDigits[(c >> 4) & 0x0F]);
        out.push_back(kHexDigits[c & 0x0F]);
    }
    return out;
}

#if BLUE_SCAN_HAS_THEENGS
template <typename DecoderT>
static auto decoder_invoke(DecoderT &decoder, DynamicJsonDocument &doc, JsonObject &obj, int)
    -> decltype(decoder.decodeBLEJson(obj), bool())
{
    (void)doc;
    return decoder.decodeBLEJson(obj);
}

template <typename DecoderT>
static auto decoder_invoke(DecoderT &decoder, DynamicJsonDocument &doc, JsonObject &obj, long)
    -> decltype(decoder.decodeBLEJson(doc), bool())
{
    (void)obj;
    return decoder.decodeBLEJson(doc);
}

static bool decode_with_theengs(const std::string &addr,
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

    char buf[96] = {0};
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

    if (!has_metric) return false;
    return true;
}
#endif

class BlueScanAdvertisedDeviceCallbacks : public BLEAdvertisedDeviceCallbacks {
public:
    void onResult(BLEAdvertisedDevice advertisedDevice) override
    {
        BlueScanService::instance().update_from_advertised_device(advertisedDevice);
    }
};

static BlueScanAdvertisedDeviceCallbacks g_adv_cb;
#endif

}  // namespace

BlueScanService &BlueScanService::instance()
{
    static BlueScanService inst;
    return inst;
}

bool BlueScanService::begin()
{
    for (auto &device : _devices) device = Device{};
#if defined(ARDUINO) && defined(ESP_PLATFORM)
    if (!_adv_queue)
        _adv_queue = xQueueCreate(ADV_QUEUE_DEPTH, sizeof(RawAdv));
#endif
    return true;
}

void BlueScanService::loop()
{
    if (!_running) return;
#if defined(ARDUINO) && defined(ESP_PLATFORM)
    drain_adv_queue();
#else
    update_simulated_devices();
#endif
}

void BlueScanService::start()
{
    if (_running) return;
    _running = true;

#if defined(ARDUINO) && defined(ESP_PLATFORM)
    start_hardware_scan();
#else
    seed_simulated_devices();
#endif
}

void BlueScanService::stop()
{
    if (!_running) return;
    _running = false;

#if defined(ARDUINO) && defined(ESP_PLATFORM)
    stop_hardware_scan();
#endif
}

const char *BlueScanService::status_text() const
{
    if (!_running) return "Scanner stopped";
#if defined(ARDUINO) && defined(ESP_PLATFORM)
#if BLUE_SCAN_HAS_THEENGS
    return "Scanning BLE + decoding telemetry...";
#else
    return "Scanning BLE (decoder library unavailable)";
#endif
#else
    return "Simulated BLE scan (native test mode)";
#endif
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
#if defined(ARDUINO)
    return millis();
#else
    using namespace std::chrono;
    return (unsigned long)duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
#endif
}

#if defined(ARDUINO) && defined(ESP_PLATFORM)
void BlueScanService::start_hardware_scan()
{
    if (!g_scan) {
        BLEDevice::init("");
        g_scan = BLEDevice::getScan();
        g_scan->setAdvertisedDeviceCallbacks(&g_adv_cb, false);
        g_scan->setInterval(90);
        g_scan->setWindow(70);
        g_scan->setActiveScan(true);
    }

    if (g_scan) {
        g_scan->clearResults();
        g_scan->start(0, nullptr, false);
    }
}

void BlueScanService::stop_hardware_scan()
{
    if (g_scan) g_scan->stop();
}

void BlueScanService::update_from_advertised_device(BLEAdvertisedDevice advertised)
{
    // This method runs on the BTC_TASK which has a very small stack.
    // Only copy raw bytes into a fixed-size struct and enqueue; do NO heap-heavy
    // work (ArduinoJson, TheengsDecoder) here. drain_adv_queue() processes the
    // queue on the main loop task where the stack is large enough.
    if (!_adv_queue) return;

    RawAdv raw{};

    const std::string addr_str = advertised.getAddress().toString();
    strncpy(raw.addr, addr_str.c_str(), sizeof(raw.addr) - 1);

    if (advertised.haveName()) {
        const std::string name_str = advertised.getName();
        strncpy(raw.name, name_str.c_str(), sizeof(raw.name) - 1);
    } else {
        strncpy(raw.name, "Unnamed", sizeof(raw.name) - 1);
    }

    raw.rssi = advertised.getRSSI();

    if (advertised.haveManufacturerData()) {
        const std::string mfr_str = advertised.getManufacturerData();
        raw.mfr_len = (uint8_t)std::min(mfr_str.size(), sizeof(raw.mfr));
        memcpy(raw.mfr, mfr_str.data(), raw.mfr_len);
    }

    const int svc_n = advertised.getServiceDataCount();
    raw.svc_count = (uint8_t)std::min(svc_n, (int)(sizeof(raw.svc) / sizeof(raw.svc[0])));
    for (int i = 0; i < (int)raw.svc_count; ++i) {
        const std::string uuid_str = advertised.getServiceDataUUID(i).toString();
        strncpy(raw.svc[i].uuid, uuid_str.c_str(), sizeof(raw.svc[i].uuid) - 1);
        const std::string data_str = advertised.getServiceData(i);
        raw.svc[i].data_len = (uint8_t)std::min(data_str.size(), sizeof(raw.svc[i].data));
        memcpy(raw.svc[i].data, data_str.data(), raw.svc[i].data_len);
    }

    // Non-blocking: drop the advertisement if the queue is full rather than
    // blocking the BTC_TASK.
    xQueueSend((QueueHandle_t)_adv_queue, &raw, 0);
}

void BlueScanService::drain_adv_queue()
{
    if (!_adv_queue) return;
    RawAdv raw{};
    while (xQueueReceive((QueueHandle_t)_adv_queue, &raw, 0) == pdTRUE) {
        const std::string addr(raw.addr);
        const int slot = find_slot_for_addr(addr);

        Device &row = _devices[slot];
        row.valid = true;
        row.addr = addr;
        row.name = raw.name;
        row.rssi = raw.rssi;
        row.last_seen_ms = now_ms();

#if BLUE_SCAN_HAS_THEENGS
        const std::string mfr_str(reinterpret_cast<const char *>(raw.mfr), raw.mfr_len);
        std::vector<std::pair<std::string, std::string>> service_data;
        for (int i = 0; i < (int)raw.svc_count; ++i) {
            service_data.emplace_back(
                raw.svc[i].uuid,
                std::string(reinterpret_cast<const char *>(raw.svc[i].data), raw.svc[i].data_len));
        }

        std::string decoded;
        if (decode_with_theengs(addr, row.rssi, mfr_str, service_data, decoded))
            row.decoded = decoded;
        else
            row.decoded = "No decoded metrics";
#else
        row.decoded = "No decoder match";
#endif
    }
}
#else
void BlueScanService::seed_simulated_devices()
{
    for (auto &device : _devices) device = Device{};

    _devices[0] = Device{true, "A4:C1:38:7D:22:90", "LYWSD03MMC", -63, "Temp 21.8 C | Hum 47% | Batt 91%", now_ms()};
    _devices[1] = Device{true, "70:04:1D:5A:3E:11", "IBS-TH2", -71, "Temp 19.4 C | Hum 54% | Batt 84%", now_ms()};
    _last_simulated_update_ms = now_ms();
}

void BlueScanService::update_simulated_devices()
{
    const unsigned long now = now_ms();
    if ((now - _last_simulated_update_ms) < 3000UL) return;
    _last_simulated_update_ms = now;

    // Softly vary values to mimic live scan updates.
    if (_devices[0].valid) {
        _devices[0].rssi = (_devices[0].rssi <= -60) ? -58 : -64;
        _devices[0].decoded = (_devices[0].decoded.find("21.8") != std::string::npos)
            ? "Temp 22.0 C | Hum 46% | Batt 91%"
            : "Temp 21.8 C | Hum 47% | Batt 91%";
        _devices[0].last_seen_ms = now;
    }

    if (_devices[1].valid) {
        _devices[1].rssi = (_devices[1].rssi <= -70) ? -68 : -72;
        _devices[1].decoded = (_devices[1].decoded.find("19.4") != std::string::npos)
            ? "Temp 19.7 C | Hum 53% | Batt 84%"
            : "Temp 19.4 C | Hum 54% | Batt 84%";
        _devices[1].last_seen_ms = now;
    }
}
#endif

SERVICE_REGISTER(BlueScanService);
