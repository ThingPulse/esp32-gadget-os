#pragma once

#include "ServiceRegistry.h"
#include <atomic>
#include <cstdint>
#include <string>

class DeviceManagementService : public SystemService {
public:
    static DeviceManagementService &instance();
    const char *name() const override { return "DeviceManagement"; }
    bool begin() override;
    void loop() override;

private:
#if defined(ARDUINO) && defined(ESP_PLATFORM)
    bool load_identity();
    static void check_in_task(void *context);
    bool _enabled = false;
    bool _scheduled = false;
    bool _due = false;
    bool _running = false;
    std::atomic<bool> _finished{false};
    uint32_t _deadline = 0;
    uint32_t _backoff = 60000;
    int _status = 0;
    uint32_t _server_delay = 0;
    std::string _identity;
    std::string _payload;
#endif
};
