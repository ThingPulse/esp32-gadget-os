#pragma once

#include "ServiceRegistry.h"
#include "services/WeatherLocationStore.h"

#include <string>
#include <vector>

class WeatherLocationSearchService : public SystemService {
public:
    static WeatherLocationSearchService &instance();
    const char *name() const override { return "WeatherLocationSearch"; }
    bool begin() override;
    void loop() override;

    bool search(const std::string &query);
    bool is_fetching() const { return _fetching; }
    unsigned long revision() const { return _revision; }
    const std::vector<WeatherLocation> &results() const { return _results; }
    const std::string &error() const { return _error; }

private:
    bool start_fetch(const std::string &query);
    void process_result();
#if defined(ARDUINO)
    static void task_entry(void *user_data);
#endif

    volatile bool _fetching = false;
    unsigned long _revision = 0;
    std::string _pending_query;
    std::vector<WeatherLocation> _results;
    std::string _error;
};
