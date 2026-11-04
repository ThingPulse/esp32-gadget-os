#pragma once
#include <ArduinoJson.h>
#include <cstdint>
namespace AppUsage {
// Optional, UI-thread-only aggregate telemetry. Never stores app content.
bool enabled();
void set_enabled(bool);
void route(const char *);
void native_start(const char *id,const char *version,bool trusted);
void stop();
void installation(const char *id,const char *version,bool trusted,bool success);
void poll();
void flush();
void snapshot(JsonObject);
uint32_t report_token();
bool report_allowed(uint32_t);
}
