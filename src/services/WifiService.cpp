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

constexpr unsigned long CONNECT_TIMEOUT_MS = 15000;
constexpr unsigned long RETRY_INITIAL_MS = 5000;
constexpr unsigned long RETRY_MAX_MS = 60000;

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
    if (_scan_suspended)
        return;

#if defined(ARDUINO)
    const wl_status_t wifi_status = WiFi.status();
    if (wifi_status == WL_CONNECTED) {
        _retry_delay_ms = RETRY_INITIAL_MS;
        _retry_at_ms = 0;
        set_state(State::Connected);
        return;
    }

    if (_state == State::Connected) {
        set_state(State::Disconnected);
        schedule_retry();
        return;
    }

    if (_state == State::Connecting) {
        if (now_ms() - _connect_started_ms > CONNECT_TIMEOUT_MS) {
            WiFi.disconnect(false);
            set_state(State::Disconnected);
            schedule_retry();
        }
        return;
    }

    if (!_ssid.empty() && _retry_at_ms != 0 &&
        static_cast<long>(now_ms() - _retry_at_ms) >= 0)
        start_connection();
#else
    if (_state == State::Connecting && now_ms() - _connect_started_ms > 2000)
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
        _retry_at_ms = 0;
        return false;
    }

    _retry_delay_ms = RETRY_INITIAL_MS;
    start_connection();
    return true;
}

void WifiService::start_connection()
{
#if defined(ARDUINO)
    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(true);
    WiFi.begin(_ssid.c_str(), _password.c_str());
#endif

    _retry_at_ms = 0;
    _connect_started_ms = now_ms();
    set_state(State::Connecting);
}

void WifiService::schedule_retry()
{
    _retry_at_ms = now_ms() + _retry_delay_ms;
    if (_retry_delay_ms < RETRY_MAX_MS) {
        _retry_delay_ms *= 2;
        if (_retry_delay_ms > RETRY_MAX_MS)
            _retry_delay_ms = RETRY_MAX_MS;
    }
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

void WifiService::begin_network_scan()
{
    if (_scan_suspended)
        return;

    _scan_suspended = true;
    _resume_connection_after_scan = !_ssid.empty() && _state != State::Connected;
#if defined(ARDUINO)
    if (_state == State::Connecting)
        WiFi.disconnect(false);
#endif
    if (_state == State::Connecting) {
        _retry_at_ms = 0;
        set_state(State::Disconnected);
    }
}

void WifiService::end_network_scan()
{
    if (!_scan_suspended)
        return;

    _scan_suspended = false;
    const bool reconnect = _resume_connection_after_scan;
    _resume_connection_after_scan = false;
    if (reconnect && !_ssid.empty())
        start_connection();
}

SERVICE_REGISTER(WifiService);
