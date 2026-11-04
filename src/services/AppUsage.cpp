#include "services/AppUsage.h"
#if defined(ARDUINO) && defined(ESP_PLATFORM)
#include "services/AppUsageLedger.h"
#include "SystemVersion.h"
#include "services/DeviceCheckInPolicy.h"
#include <Arduino.h>
#include <LittleFS.h>
#include <Preferences.h>
#include <esp_random.h>
#include <cstring>
#if GADGET_OS_NATIVE_APP_LOADER
#include "experimental/NativeStore.h"
#endif
namespace AppUsage {
namespace {
bool initialized=false,consent=false,changed=false;
Ledger ledger;
ConsentGate reporting;
std::string epoch;
uint32_t saved=0;
constexpr const char *FileName="/app-usage.json",*Stage="/app-usage.tmp";
void encode(JsonObject d){
    d["schema"]=1;d["epoch"]=epoch;auto a=d["apps"].to<JsonArray>();
    for(auto &e:ledger.entries){auto r=a.add<JsonObject>();r["id"]=e.id;r["version"]=e.version;
        r["launches"]=e.launches;r["seconds"]=e.seconds;r["installs"]=e.installs;r["failures"]=e.failures;}
}
void new_epoch(){uint8_t bytes[16];esp_fill_random(bytes,sizeof bytes);epoch=DeviceCheckInPolicy::uuid(bytes);changed=true;}
void begin(){
    if(initialized)return;initialized=true;saved=millis();
    Preferences p;if(p.begin("app-usage",true)){consent=p.getBool("enabled",false);p.end();}
    reporting.set(consent);
    if(!consent)return;
    auto f=LittleFS.open(FileName,"r");JsonDocument d;
    if(f&&f.size()<=16384&&!deserializeJson(d,f)&&d["schema"]==1&&DeviceCheckInPolicy::valid_uuid(d["epoch"]|std::string())&&d["apps"].is<JsonArray>()&&d["apps"].size()<=Ledger::Capacity){
        epoch=d["epoch"].as<std::string>();
        for(JsonObject r:d["apps"].as<JsonArray>()){
            auto id=r["id"].as<std::string>(),version=r["version"].as<std::string>();
            if(id.empty()||id.size()>64||version.empty()||version.size()>64)continue;
            ledger.entries.push_back({id,version,r["launches"]|0U,r["seconds"]|0U,r["installs"]|0U,r["failures"]|0U});
        }
    }else new_epoch();
}
}
bool enabled(){begin();return consent;}
uint32_t report_token(){return reporting.token();}
bool report_allowed(uint32_t token){return reporting.accepts(token);}
void set_enabled(bool value){
    begin();if(value==consent)return;
    Preferences p;if(!p.begin("app-usage",false))return;
    if(p.putBool("enabled",value)!=1){p.end();return;}p.end();consent=value;reporting.set(value);
    ledger=Ledger{};epoch.clear();LittleFS.remove(FileName);LittleFS.remove(Stage);
    changed=false;if(consent){new_epoch();flush();}
}
void route(const char *name){
    if(!enabled())return;
    ledger.stop(millis());changed=true;
    // Explicit allow-list: no routes containing user settings/content are reported.
    for(auto known:{"weather","stocks","transit","planes","home_assistant","iss","calculator","dice_game"})
        if(name&&!strcmp(name,known)){ledger.start(std::string("builtin.")+known,GADGET_OS_VERSION,millis());break;}
}
void native_start(const char *id,const char *version,bool trusted){
    if(!enabled())return;ledger.stop(millis());changed=true;
    if(trusted)ledger.start(std::string("native.")+id,version,millis());
}
void stop(){if(enabled()){ledger.stop(millis());changed=true;}}
void installation(const char *id,const char *version,bool trusted,bool success){
    if(!enabled()||!trusted)return;
    auto &e=ledger.get(std::string("native.")+id,version);Ledger::add(success?e.installs:e.failures);changed=true;
}
void flush(){
    if(!enabled())return;ledger.tick(millis());
    if(!changed&&ledger.active<0)return;
    saved=millis(); // Bound retry/write frequency even when storage is full.
    JsonDocument d;encode(d.to<JsonObject>());auto f=LittleFS.open(Stage,"w");
    if(!f)return;bool ok=serializeJson(d,f)==measureJson(d);f.flush();f.close();
    if(ok&&LittleFS.rename(Stage,FileName)){changed=false;saved=millis();}else LittleFS.remove(Stage);
}
void poll(){if(enabled()){ledger.tick(millis());if(millis()-saved>=300000)flush();}}
void snapshot(JsonObject d){
    if(!enabled())return;flush();encode(d);
    auto installed=d["installed"].to<JsonArray>();
#if GADGET_OS_NATIVE_APP_LOADER
    native_app::store::usage_inventory(installed);
#endif
}
}
#else
namespace AppUsage {
bool enabled(){return false;}void set_enabled(bool){}void route(const char*){}
void native_start(const char*,const char*,bool){}void stop(){}
void installation(const char*,const char*,bool,bool){}void poll(){}void flush(){}void snapshot(JsonObject){}
uint32_t report_token(){return 0;}bool report_allowed(uint32_t){return false;}
}
#endif
