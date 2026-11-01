#pragma once
#include "ServiceRegistry.h"
#include <atomic>
#include <cstdint>
#include <string>
#include <vector>
namespace iss {
struct Pass {
    int64_t rise=0, peak=0, set=0, visible_start=0, visible_end=0;
    double rise_azimuth=0, set_azimuth=0, elevation=0;
    bool visible=false;
};
struct Forecast {
    std::vector<Pass> passes;
    bool stale=false;
    int64_t generated=0;
    std::string error;
    uint32_t retry_ms=0;
};
Forecast parse(const std::string &body);
void combine(Forecast &into,const Forecast &other);
const Pass *next(const Forecast &forecast,int64_t now,bool visible_only);
const char *compass(double azimuth);
std::string duration(int64_t seconds);
}
class IssService : public SystemService {
public:
    static IssService &instance();
    const char *name() const override {return "ISS";}
    bool begin() override {return true;}
    void loop() override;
    void resume();
    void pause() {_active=false;}
    void refresh();
    const iss::Forecast &forecast() const {return _forecast;}
    const std::string &location() const {return _location;}
    const std::string &error() const {return _error;}
    bool configured() const {return !_key.empty();}
    bool fetching() const {return _running;}
    int64_t fetched() const {return _fetched;}
    unsigned revision() const {return _revision;}
private:
    static void worker(void *self);
    static uint32_t now_ms();
    void load_cache();
    std::string _location,_key,_job_key,_error;
    double _lat=0,_lon=0,_job_lat=0,_job_lon=0;
    bool _active=false,_running=false,_pending=false,_gate=false;
    uint32_t _allowed=0,_backoff=60000;
    int64_t _fetched=0;
    unsigned _revision=0;
    iss::Forecast _forecast,_result;
    std::string _all_body,_visible_body;
    std::atomic<bool> _finished{false};
};
