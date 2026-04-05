#pragma once

#include "ServiceRegistry.h"
#include "EventBus.h"
#include <string>

class TimeService : public SystemService {
public:
    static TimeService &instance();

    const char *name() const override { return "Time"; }
    bool begin() override;
    void loop() override;

    bool is_synchronized() const;
    std::string time_hhmm() const;

    static int timezone_count();
    static const char *timezone_name(int index);
    static int region_count();
    static const char *region_name(int index);
    static int timezone_count_in_region(int region_index);
    static const char *timezone_name_in_region(int region_index, int timezone_index_in_region);
    static int timezone_global_index(int region_index, int timezone_index_in_region);
    static void split_timezone_index(int global_index, int *out_region_index, int *out_timezone_index_in_region);

    int timezone_index() const;
    bool use_24h() const;
    bool save_preferences(int tz_index, bool use_24h);

private:
    static void on_wifi_connected(const SystemEvent &event, void *user_data);

    void start_sync();
    void finish_sync();
    void load_preferences();
    void apply_timezone();
    unsigned long now_ms() const;

    bool _sync_in_progress = false;
    bool _synchronized = false;
    unsigned long _sync_start_ms = 0;
    SubscriptionId _wifi_connected_sub = 0;
    int _timezone_index = 0;
    bool _use_24h = true;
};