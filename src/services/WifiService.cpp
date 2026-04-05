#include "services/WifiService.h"

#include "EventBus.h"
#include "services/ConfigStoreService.h"
#include "services/FileSystemService.h"

#if defined(ARDUINO)
#include <WiFi.h>
#else
#include <chrono>
#endif

namespace {

unsigned long now_ms()
{
#if defined(ARDUINO)
    return millis();
#else
    using namespace std::chrono;
    return static_cast<unsigned long>(duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count());
#endif
}

}  // namespace

WifiService &WifiService::instance()
{
    static WifiService inst;
    return inst;
}

bool WifiService::begin()
{
    return reload_from_config();
}

void WifiService::loop()
{
    if (_state != State::Connecting)
        return;

#if defined(ARDUINO)
    if (WiFi.status() == WL_CONNECTED) {
        set_state(State::Connected);
        return;
    }

    if (now_ms() - _connect_started_ms > 15000)
        set_state(State::Disconnected);
#else
    if (now_ms() - _connect_started_ms > 2000)
        set_state(State::Connected);
#endif
}

bool WifiService::reload_from_config()
{
    ConfigStoreService &cfg = ConfigStoreService::instance();
    if (!cfg.begin()) {
        set_state(State::Disconnected);
        return false;
    }

    cfg.load("wifi");
    std::string ssid = cfg.get("ssid", "");
    std::string password = cfg.get("password", "");
    return connect_with_credentials(ssid, password);
}

bool WifiService::connect_with_credentials(const std::string &ssid, const std::string &password)
{
    _ssid = ssid;
    _password = password;

    if (_ssid.empty()) {
#if defined(ARDUINO)
        WiFi.disconnect();
#endif
        set_state(State::Disconnected);
        return false;
    }

#if defined(ARDUINO)
    WiFi.mode(WIFI_STA);
    WiFi.begin(_ssid.c_str(), _password.c_str());
#endif

    _connect_started_ms = now_ms();
    set_state(State::Connecting);
    return true;
}

void WifiService::set_state(State next_state)
{
    if (_state == next_state)
        return;

    _state = next_state;

    SystemEvent event{};
    if (_state == State::Connecting)
        event.type = EventType::WifiConnecting;
    else if (_state == State::Connected)
        event.type = EventType::WifiConnected;
    else
        event.type = EventType::WifiDisconnected;

    EventBus::instance().publish(event);
}

bool WifiService::is_connected() const
{
    return _state == State::Connected;
}

bool WifiService::is_connecting() const
{
    return _state == State::Connecting;
}

SERVICE_REGISTER(WifiService);