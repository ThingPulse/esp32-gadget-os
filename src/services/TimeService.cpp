#include "services/TimeService.h"

#include "EventBus.h"
#include "services/ConfigStoreService.h"
#include "services/WifiService.h"

#if defined(ARDUINO)
#include <Arduino.h>
#include <cstdlib>
#include <time.h>
#else
#include <chrono>
#include <cstdlib>
#include <ctime>
#endif

namespace {

constexpr const char *NTP_SERVER = "pool.ntp.org";

struct TimezoneEntry {
    const char *name;
    const char *posix_tz;
};

constexpr TimezoneEntry TIMEZONES[] = {
    {"UTC", "UTC0"},
    {"Europe/Berlin", "CET-1CEST,M3.5.0,M10.5.0/3"},
    {"Europe/Paris", "CET-1CEST,M3.5.0,M10.5.0/3"},
    {"Europe/Amsterdam", "CET-1CEST,M3.5.0,M10.5.0/3"},
    {"Europe/Madrid", "CET-1CEST,M3.5.0,M10.5.0/3"},
    {"Europe/Rome", "CET-1CEST,M3.5.0,M10.5.0/3"},
    {"Europe/Vienna", "CET-1CEST,M3.5.0,M10.5.0/3"},
    {"Europe/Zurich", "CET-1CEST,M3.5.0,M10.5.0/3"},
    {"Europe/Prague", "CET-1CEST,M3.5.0,M10.5.0/3"},
    {"Europe/Warsaw", "CET-1CEST,M3.5.0,M10.5.0/3"},
    {"Europe/Athens", "EET-2EEST,M3.5.0/3,M10.5.0/4"},
    {"Europe/Helsinki", "EET-2EEST,M3.5.0/3,M10.5.0/4"},
    {"Europe/Bucharest", "EET-2EEST,M3.5.0/3,M10.5.0/4"},
    {"Europe/London", "GMT0BST,M3.5.0/1,M10.5.0"},
    {"Europe/Dublin", "GMT0IST,M3.5.0/1,M10.5.0"},
    {"Europe/Lisbon", "WET0WEST,M3.5.0/1,M10.5.0"},
    {"Europe/Moscow", "MSK-3"},
    {"Africa/Cairo", "EET-2"},
    {"Africa/Johannesburg", "SAST-2"},
    {"Africa/Nairobi", "EAT-3"},
    {"America/St_Johns", "NST3:30NDT,M3.2.0,M11.1.0"},
    {"America/Halifax", "AST4ADT,M3.2.0,M11.1.0"},
    {"America/New_York", "EST5EDT,M3.2.0,M11.1.0"},
    {"America/Detroit", "EST5EDT,M3.2.0,M11.1.0"},
    {"America/Toronto", "EST5EDT,M3.2.0,M11.1.0"},
    {"America/Chicago", "CST6CDT,M3.2.0,M11.1.0"},
    {"America/Winnipeg", "CST6CDT,M3.2.0,M11.1.0"},
    {"America/Denver", "MST7MDT,M3.2.0,M11.1.0"},
    {"America/Phoenix", "MST7"},
    {"America/Los_Angeles", "PST8PDT,M3.2.0,M11.1.0"},
    {"America/Vancouver", "PST8PDT,M3.2.0,M11.1.0"},
    {"America/Anchorage", "AKST9AKDT,M3.2.0,M11.1.0"},
    {"Pacific/Honolulu", "HST10"},
    {"America/Mexico_City", "CST6"},
    {"America/Bogota", "COT5"},
    {"America/Lima", "PET5"},
    {"America/Santiago", "CLT4CLST,M9.1.6/24,M4.1.6/24"},
    {"America/Sao_Paulo", "BRT3"},
    {"America/Buenos_Aires", "ART3"},
    {"Asia/Dubai", "GST-4"},
    {"Asia/Karachi", "PKT-5"},
    {"Asia/Kolkata", "IST-5:30"},
    {"Asia/Dhaka", "BST-6"},
    {"Asia/Bangkok", "ICT-7"},
    {"Asia/Tokyo", "JST-9"},
    {"Asia/Shanghai", "CST-8"},
    {"Asia/Hong_Kong", "HKT-8"},
    {"Asia/Singapore", "SGT-8"},
    {"Asia/Seoul", "KST-9"},
    {"Pacific/Auckland", "NZST-12NZDT,M9.5.0,M4.1.0/3"},
    {"Australia/Sydney", "AEST-10AEDT,M10.1.0,M4.1.0/3"},
    {"Australia/Melbourne", "AEST-10AEDT,M10.1.0,M4.1.0/3"},
    {"Australia/Brisbane", "AEST-10"},
    {"Australia/Adelaide", "ACST-9:30ACDT,M10.1.0,M4.1.0/3"},
    {"Australia/Perth", "AWST-8"},
};

constexpr int TIMEZONE_COUNT = static_cast<int>(sizeof(TIMEZONES) / sizeof(TIMEZONES[0]));

}  // namespace

TimeService &TimeService::instance()
{
    static TimeService inst;
    return inst;
}

bool TimeService::begin()
{
    load_preferences();
    apply_timezone();

    if (_wifi_connected_sub == 0)
        _wifi_connected_sub = EventBus::instance().subscribe(EventType::WifiConnected, on_wifi_connected, this);

    if (WifiService::instance().is_connected())
        start_sync();

    return true;
}

void TimeService::loop()
{
    if (!_sync_in_progress)
        return;

#if defined(ARDUINO)
    time_t now = time(nullptr);
    if (now > 1600000000) {
        finish_sync();
        return;
    }

    if (now_ms() - _sync_start_ms > 15000)
        _sync_in_progress = false;
#else
    if (now_ms() - _sync_start_ms > 1500)
        finish_sync();
#endif
}

bool TimeService::is_synchronized() const
{
    return _synchronized;
}

std::string TimeService::time_hhmm() const
{
    if (!_synchronized)
        return "--:--";

    char buf[12] = "--:--";
    time_t now = time(nullptr);
    struct tm tm_info{};
    localtime_r(&now, &tm_info);

    if (_use_24h)
        strftime(buf, sizeof(buf), "%H:%M", &tm_info);
    else
        strftime(buf, sizeof(buf), "%I:%M %p", &tm_info);

    return buf;
}

int TimeService::timezone_count()
{
    return TIMEZONE_COUNT;
}

const char *TimeService::timezone_name(int index)
{
    if (index < 0 || index >= TIMEZONE_COUNT)
        return TIMEZONES[0].name;
    return TIMEZONES[index].name;
}

int TimeService::timezone_index() const
{
    return _timezone_index;
}

bool TimeService::use_24h() const
{
    return _use_24h;
}

bool TimeService::save_preferences(int tz_index, bool use_24h_pref)
{
    if (tz_index < 0) tz_index = 0;
    if (tz_index >= TIMEZONE_COUNT) tz_index = TIMEZONE_COUNT - 1;

    ConfigStoreService &cfg = ConfigStoreService::instance();
    if (!cfg.begin())
        return false;

    cfg.load("time");
    cfg.set("timezone_index", tz_index);
    cfg.set("use_24h", use_24h_pref);
    cfg.save();

    _timezone_index = tz_index;
    _use_24h = use_24h_pref;
    apply_timezone();

    if (_synchronized)
        EventBus::instance().publish(SystemEvent{EventType::TimeSynchronized});

    return true;
}

void TimeService::on_wifi_connected(const SystemEvent &event, void *user_data)
{
    (void)event;
    TimeService *self = static_cast<TimeService *>(user_data);
    if (self)
        self->start_sync();
}

void TimeService::start_sync()
{
    _sync_in_progress = true;
    _sync_start_ms = now_ms();

#if defined(ARDUINO)
    configTzTime(TIMEZONES[_timezone_index].posix_tz, NTP_SERVER);
#endif
}

void TimeService::finish_sync()
{
    _sync_in_progress = false;
    if (_synchronized)
        return;

    _synchronized = true;
    EventBus::instance().publish(SystemEvent{EventType::TimeSynchronized});
}

unsigned long TimeService::now_ms() const
{
#if defined(ARDUINO)
    return millis();
#else
    using namespace std::chrono;
    return static_cast<unsigned long>(duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count());
#endif
}

void TimeService::load_preferences()
{
    ConfigStoreService &cfg = ConfigStoreService::instance();
    if (!cfg.begin())
        return;

    cfg.load("time");
    int tz = cfg.get_int("timezone_index", 0);
    bool use24 = cfg.get_bool("use_24h", true);

    if (tz < 0) tz = 0;
    if (tz >= TIMEZONE_COUNT) tz = TIMEZONE_COUNT - 1;

    _timezone_index = tz;
    _use_24h = use24;
}

void TimeService::apply_timezone()
{
    const char *tz = TIMEZONES[_timezone_index].posix_tz;
    setenv("TZ", tz, 1);
    tzset();
}

SERVICE_REGISTER(TimeService);