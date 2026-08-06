#pragma once

#include "ServiceRegistry.h"
#include <string>

class UpdateService : public SystemService {
public:
    enum class Channel { Stable, Prerelease };
    enum class State { Idle, Checking, Available, Downloading, ReadyToReboot, Error };

    static UpdateService &instance();
    const char *name() const override { return "Update"; }
    bool begin() override;
    void loop() override;

    bool check(bool manual = false);
    bool install();
    void ignore_available();
    void dismiss();

    Channel channel() const { return _channel; }
    void set_channel(Channel value);
    State state() const { return _state; }
    int progress_percent() const { return _progress; }
    const std::string &available_version() const { return _available_version; }
    const std::string &release_notes() const { return _release_notes; }
    const std::string &error() const { return _error; }
    bool manual_check() const { return _manual_check; }
    static const char *current_version();
    static bool supported();

private:
    static void wifi_connected(const struct SystemEvent &, void *user_data);
#if defined(ARDUINO) && defined(ESP_PLATFORM)
    static void check_task(void *user_data);
    static void install_task(void *user_data);
    bool fetch_manifest();
    bool download_and_install();
#endif
    void load_preferences();
    void publish_if_changed();

    volatile State _state = State::Idle;
    volatile int _progress = 0;
    State _published_state = State::Idle;
    Channel _channel = Channel::Stable;
    bool _manual_check = false;
    bool _checked_this_boot = false;
    int _wifi_sub = 0;
    std::string _ignored_version;
    std::string _available_version;
    std::string _release_notes;
    std::string _firmware_url;
    std::string _sha256;
    size_t _firmware_size = 0;
    std::string _error;
};
