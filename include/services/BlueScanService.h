#pragma once

#include "ServiceRegistry.h"

#include <array>
#include <string>

class BlueScanService : public SystemService {
public:
    static constexpr int MAX_DEVICES = 8;

    struct Device {
        bool valid = false;
        std::string addr;
        std::string name;
        int rssi = 0;
        std::string decoded;
        unsigned long last_seen_ms = 0;
    };

    static BlueScanService &instance();

    const char *name() const override { return "BlueScan"; }
    bool begin() override;
    void loop() override;

    void start();
    void stop();

#if defined(ARDUINO) && defined(ESP_PLATFORM)
    void update_from_advertised_device(class BLEAdvertisedDevice advertised);
#endif

    bool is_running() const { return _running; }
    const char *status_text() const;
    const std::array<Device, MAX_DEVICES> &devices() const { return _devices; }

private:
    BlueScanService() = default;

    int find_slot_for_addr(const std::string &addr);
    unsigned long now_ms() const;

#if defined(ARDUINO) && defined(ESP_PLATFORM)
    void start_hardware_scan();
    void stop_hardware_scan();
#else
    void seed_simulated_devices();
    void update_simulated_devices();
    unsigned long _last_simulated_update_ms = 0;
#endif

    bool _running = false;
    std::array<Device, MAX_DEVICES> _devices{};
};
