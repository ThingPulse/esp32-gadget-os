#include "services/DeviceManagementService.h"
#include "services/DeviceCheckInPolicy.h"
#include "ui/UiTrace.h"

#if defined(ARDUINO) && defined(ESP_PLATFORM)
#include "DeviceManagementBuildConfig.h"
#include "SystemVersion.h"
#include "services/TimeService.h"
#include "services/UpdateService.h"
#include "services/WifiService.h"
#include <Arduino.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <Preferences.h>
#include <WiFiClientSecure.h>
#include <esp_random.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

namespace {
constexpr char Endpoint[] = "https://devices.thingpulse.com/api/v1/check-in";
#if defined(COLOR_KIT_PRO)
constexpr char Hardware[] = "color-kit-pro";
#else
constexpr char Hardware[] = "color-kit-grande";
#endif
}
#endif

DeviceManagementService &DeviceManagementService::instance()
{
    static DeviceManagementService service;
    return service;
}

bool DeviceManagementService::begin()
{
#if defined(ARDUINO) && defined(ESP_PLATFORM)
    _enabled = sizeof(GADGET_OS_DEVICE_PRODUCT_KEY) == 65;
    UI_TRACE("Devices", "%s", _enabled ? "Background check-ins enabled" : "Check-ins disabled: no build product key");
#endif
    // Telemetry must never make system startup fail.
    return true;
}

void DeviceManagementService::loop()
{
#if defined(ARDUINO) && defined(ESP_PLATFORM)
    if (!_enabled) return;
    const uint32_t now = millis();
    if (_running) {
        if (!_finished.load(std::memory_order_acquire)) return;
        _running = false;
        const uint32_t delay = DeviceCheckInPolicy::next_delay(_status, _server_delay, esp_random(), _backoff);
        _deadline = now + delay;
        _due = false;
        UI_TRACE("Devices", "Check-in status=%d; next attempt in %u seconds", _status, unsigned(delay / 1000));
    }
    // Latch expiry even while offline so weeks without Wi-Fi cannot leave an
    // old deadline on the wrong side of the signed millis wrap comparison.
    if (_scheduled && DeviceCheckInPolicy::due(now, _deadline)) _due = true;
    // Avoid TLS until Wi-Fi and a certificate-validating system clock are ready.
    if (!WifiService::instance().is_connected() || !TimeService::instance().is_synchronized()) return;
    if (!_scheduled) {
        _scheduled = true;
        _deadline = now + 30000U + esp_random() % 60001U;
    }
    if (!_due) return;
    const auto update = UpdateService::instance().state();
    if (update == UpdateService::State::Checking || update == UpdateService::State::Downloading ||
        update == UpdateService::State::ReadyToReboot) return;
    if (_identity.empty() && !load_identity()) {
        _deadline = now + DeviceCheckInPolicy::MaxBackoff;
        _due = false;
        UI_TRACE("Devices", "Cannot persist installation ID; check-in deferred");
        return;
    }
    // Snapshot shared settings on the main loop; the worker owns only this payload.
    JsonDocument body;
    body["product"] = "gadget-os";
    body["installationId"] = _identity;
    body["hardware"] = Hardware;
    body["version"] = GADGET_OS_VERSION;
    body["channel"] = UpdateService::instance().channel() == UpdateService::Channel::Prerelease ? "prerelease" : "stable";
    _payload.clear();
    serializeJson(body, _payload);
    _finished.store(false, std::memory_order_relaxed);
    _running = true;
    if (xTaskCreate(check_in_task, "device_checkin", 8192, this, 1, nullptr) != pdPASS) {
        _running = false;
        _deadline = now + DeviceCheckInPolicy::next_delay(-1, 0, esp_random(), _backoff);
        _due = false;
    }
#endif
}

#if defined(ARDUINO) && defined(ESP_PLATFORM)
bool DeviceManagementService::load_identity()
{
    Preferences preferences;
    if (!preferences.begin("gadget-devices", false)) return false;
    std::string id = preferences.getString("installation", "").c_str();
    if (!DeviceCheckInPolicy::valid_uuid(id)) {
        // Called only after Wi-Fi is running, when the hardware RNG has RF entropy.
        uint8_t bytes[16];
        esp_fill_random(bytes, sizeof(bytes));
        id = DeviceCheckInPolicy::uuid(bytes);
        const bool written = preferences.putString("installation", id.c_str()) == id.size();
        const bool verified = id == preferences.getString("installation", "").c_str();
        if (!written || !verified) { preferences.end(); return false; }
    }
    preferences.end();
    _identity = id;
    return true;
}

void DeviceManagementService::check_in_task(void *context)
{
    auto *self = static_cast<DeviceManagementService *>(context);
    int status = -1;
    uint32_t server_delay = 0;
    {
        WiFiClientSecure client;
        client.useBuiltinCACertBundle();
        client.setHandshakeTimeout(8);
        HTTPClient http;
        http.setConnectTimeout(5000);
        http.setTimeout(8000);
        // Do not forward product credentials to redirects or alternate hosts.
        http.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);
        if (http.begin(client, Endpoint)) {
            const char *headers[] = {"Retry-After"};
            http.collectHeaders(headers, 1);
            http.addHeader("Content-Type", "application/json");
            http.addHeader("Authorization", "Bearer " GADGET_OS_DEVICE_PRODUCT_KEY);
            http.addHeader("User-Agent", "gadget-os/" GADGET_OS_VERSION);
            status = http.POST(String(self->_payload.c_str()));
            // The device-management API returns Retry-After as delta seconds.
            String retry = http.header("Retry-After");
            retry.trim();
            server_delay = DeviceCheckInPolicy::retry_after_ms(retry.c_str());
            http.end();
        }
    } // Release TLS buffers before reporting completion.
    self->_status = status;
    self->_server_delay = server_delay;
    self->_finished.store(true, std::memory_order_release);
    vTaskDelete(nullptr);
}
#endif

SERVICE_REGISTER(DeviceManagementService);
