#pragma once

#include "ServiceRegistry.h"
#include <string>

class WifiService : public SystemService {
public:
    static WifiService &instance();

    const char *name() const override { return "WiFi"; }
    bool begin() override;
    void loop() override;

    bool reload_from_config();
    bool is_connected() const;
    bool is_connecting() const;

private:
    enum class State {
        Disconnected,
        Connecting,
        Connected,
    };

    bool connect_with_credentials(const std::string &ssid, const std::string &password);
    void set_state(State next_state);

    State _state = State::Disconnected;
    std::string _ssid;
    std::string _password;
    unsigned long _connect_started_ms = 0;
};