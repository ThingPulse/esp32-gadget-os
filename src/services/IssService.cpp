// Retained as the simulator parity reference and for firmware without the app loader.
#if !GADGET_OS_NATIVE_APP_LOADER
#include "services/IssService.h"
#include "services/TransitProvider.h"
#include "services/DeviceCheckInPolicy.h"
#include "services/WeatherLocationStore.h"
#include "services/FileSystemService.h"
#include "services/ConfigStoreService.h"
#include "services/WifiService.h"
#include "services/TimeService.h"
#include "services/UpdateService.h"
#include "SystemVersion.h"
#include <ArduinoJson.h>
#include <algorithm>
#include <cmath>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#if defined(ARDUINO)
#include <Arduino.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <freertos/task.h>
#else
#include <chrono>
#include <thread>
#include <fstream>
#include <iterator>
#endif
namespace {
constexpr size_t MaxBody=49152;
constexpr int64_t RefreshSeconds=1800;
constexpr const char *CachePath="/cache/iss.json";
int64_t stamp(JsonVariantConst v){return transit::timestamp(v.is<const char *>()?v.as<std::string>():"");}
bool number(JsonVariantConst v,double low,double high){return v.is<double>()&&std::isfinite(v.as<double>())&&v.as<double>()>=low&&v.as<double>()<=high;}
#if defined(ARDUINO)
class Body:public Stream {
public:
 std::string data;
 size_t write(uint8_t b)override{return write(&b,1);}
 size_t write(const uint8_t *b,size_t n)override{if(data.size()+n>MaxBody)return 0;data.append((const char*)b,n);return n;}
 int available()override{return 0;}int read()override{return -1;}int peek()override{return -1;}void flush()override{}
};
#endif
iss::Forecast request(double lat,double lon,bool visible,std::string &body) {
#if !defined(ARDUINO)
 if(const char *dir=std::getenv("GADGET_OS_ISS_FIXTURE_DIR")) {
  std::ifstream in(std::string(dir)+(visible?"/visible.json":"/all.json"));
  body.assign(std::istreambuf_iterator<char>(in),{});return iss::parse(body);
 }
#endif
 char url[240];std::snprintf(url,sizeof(url),"https://iss-api.polluxlabs.io/iss-pass?lat=%.6f&lon=%.6f&n=20&days_ahead=7&visible_only=%s",lat,lon,visible?"true":"false");
 int status=0;uint32_t retry=0;
#if defined(ARDUINO)
 WiFiClientSecure client;client.useBuiltinCACertBundle();client.setHandshakeTimeout(8);
 HTTPClient http;http.setConnectTimeout(5000);http.setTimeout(15000);http.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);
 if(http.begin(client,url)) {
  const char *headers[]={"Retry-After"};http.collectHeaders(headers,1);
  http.addHeader("User-Agent","ThingPulse-GadgetOS/" GADGET_OS_VERSION);
  status=http.GET();retry=DeviceCheckInPolicy::retry_after_ms(http.header("Retry-After").c_str());
  if(status==200){Body sink;if(http.getSize()>int(MaxBody)||http.writeToStream(&sink)<0)status=-1;else body=std::move(sink.data);}
  http.end();
 }
#else
 // Only validated numeric coordinates and a fixed boolean enter this URL.
 std::string cmd="curl -sS --max-time 25 --max-filesize 49152 -D - -w '\\n%{http_code}' '"+std::string(url)+"'";
 FILE *pipe=popen(cmd.c_str(),"r");std::string raw;bool oversized=false;
 if(pipe){char bytes[2048];while(size_t n=fread(bytes,1,sizeof(bytes),pipe)){if(raw.size()+n<=MaxBody+8192)raw.append(bytes,n);else oversized=true;}
  int exit=pclose(pipe);auto end=raw.rfind('\n');if(exit==0&&!oversized&&end!=std::string::npos){status=std::atoi(raw.c_str()+end+1);raw.resize(end);
   size_t start=0;while(raw.compare(start,5,"HTTP/")==0){size_t split=raw.find("\r\n\r\n",start);if(split==std::string::npos)break;
    auto header=raw.substr(start,split-start);auto lower=header;std::transform(lower.begin(),lower.end(),lower.begin(),[](unsigned char c){return char(std::tolower(c));});
    auto r=lower.find("retry-after:");if(r!=std::string::npos){auto value=header.substr(r+12);value=value.substr(0,value.find('\r'));auto first=value.find_first_not_of(" \t");value=first==std::string::npos?"":value.substr(first);retry=DeviceCheckInPolicy::retry_after_ms(value);}
    start=split+4;}
   body=raw.substr(start);
  }
 }
#endif
 if(status==200)return iss::parse(body);
 iss::Forecast result;result.error=status==429?"ISS rate limit - retry later":"ISS service unavailable";result.retry_ms=retry;return result;
}
}
namespace iss {
Forecast parse(const std::string &body) {
 Forecast result;JsonDocument doc;
 if(body.size()>MaxBody||deserializeJson(doc,body)||!doc["passes"].is<JsonArray>()||doc["passes"].size()>20){result.error="Invalid ISS prediction data";return result;}
 result.stale=doc["tle_stale"]|false;result.generated=stamp(doc["generated_at"]);
 for(JsonObjectConst item:doc["passes"].as<JsonArrayConst>()) {
  Pass p;p.rise=stamp(item["rise"]["time"]);p.peak=stamp(item["culmination"]["time"]);p.set=stamp(item["set"]["time"]);
  if(!p.rise||p.peak<p.rise||p.set<=p.peak||p.set-p.rise>3600||
     !number(item["rise"]["azimuth_deg"],0,360)||!number(item["set"]["azimuth_deg"],0,360)||!number(item["culmination"]["elevation_deg"],0,90)) continue;
  p.rise_azimuth=item["rise"]["azimuth_deg"];p.set_azimuth=item["set"]["azimuth_deg"];p.elevation=item["culmination"]["elevation_deg"];
  p.visible=item["visible"]|false;
  if(p.visible){p.visible_start=stamp(item["visible_start"]);p.visible_end=stamp(item["visible_end"]);
   if(p.visible_start<p.rise||p.visible_end>p.set||p.visible_end<=p.visible_start)continue;}
  result.passes.push_back(p);
 }
 if(doc["passes"].size()&&!result.passes.size())result.error="Invalid ISS prediction data";
 std::sort(result.passes.begin(),result.passes.end(),[](const Pass&a,const Pass&b){return a.rise<b.rise;});
 return result;
}
void combine(Forecast &into,const Forecast &other) {
 into.stale=into.stale||other.stale;
 for(const auto&p:other.passes)if(std::none_of(into.passes.begin(),into.passes.end(),[&](const Pass&q){return std::abs(q.rise-p.rise)<5;})&&into.passes.size()<40)into.passes.push_back(p);
 std::sort(into.passes.begin(),into.passes.end(),[](const Pass&a,const Pass&b){return a.rise<b.rise;});
}
const Pass *next(const Forecast &f,int64_t now,bool visible_only) {
 for(const auto&p:f.passes)if(p.set>now&&(!visible_only||(p.visible&&p.visible_end>now)))return &p;
 return nullptr;
}
const char *compass(double a){static const char*names[]={"N","NNE","NE","ENE","E","ESE","SE","SSE","S","SSW","SW","WSW","W","WNW","NW","NNW"};return names[int(std::lround(a/22.5))&15];}
std::string duration(int64_t seconds){seconds=std::max<int64_t>(0,seconds);char b[48];
 if(seconds>=86400)std::snprintf(b,sizeof(b),"%lldd %lldh",(long long)(seconds/86400),(long long)(seconds%86400/3600));
 else if(seconds>=3600)std::snprintf(b,sizeof(b),"%lldh %02lldm",(long long)(seconds/3600),(long long)(seconds%3600/60));
 else std::snprintf(b,sizeof(b),"%lldm %02llds",(long long)(seconds/60),(long long)(seconds%60));return b;}
}
IssService &IssService::instance(){static IssService s;return s;}
uint32_t IssService::now_ms(){
#if defined(ARDUINO)
 return millis();
#else
 return uint32_t(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
#endif
}
void IssService::resume() {
 WeatherLocation location;
 bool found=WeatherLocationStore::active(location);
 if(!found){auto &cfg=ConfigStoreService::instance();if(cfg.begin()&&cfg.load("planes")){
  auto lat=cfg.get("center_latitude","");auto lon=cfg.get("center_longitude","");
  if(!lat.empty()&&!lon.empty()){location.latitude=std::strtod(lat.c_str(),nullptr);location.longitude=std::strtod(lon.c_str(),nullptr);location.name="Your location";found=true;}}}
 if(found&&(!std::isfinite(location.latitude)||!std::isfinite(location.longitude)||std::abs(location.latitude)>90||std::abs(location.longitude)>180))found=false;
 char key[64]{};if(found)std::snprintf(key,sizeof(key),"%.6f,%.6f",location.latitude,location.longitude);
 if(_key!=key){_key=key;_forecast={};_fetched=0;_error.clear();++_revision;load_cache();}
 _location=found?location.name:"";_lat=location.latitude;_lon=location.longitude;_active=true;
 _pending=configured()&&(!_fetched||time(nullptr)-_fetched>=RefreshSeconds||_fetched>time(nullptr));
}
void IssService::load_cache(){
 auto &fs=FileSystemService::instance();if(!fs.begin())return;
 std::string raw;JsonDocument doc;
 if(!fs.fs().read(CachePath,raw)||raw.size()>2*MaxBody+8192||deserializeJson(doc,raw)||doc["key"].as<std::string>()!=_key||doc["version"].as<int>()!=1)return;
 auto all=iss::parse(doc["all"]|"");auto visible=iss::parse(doc["visible"]|"");
 if(!all.error.empty()||!visible.error.empty())return;
 iss::combine(all,visible);_forecast=std::move(all);_fetched=doc["fetched"]|int64_t(0);
}
void IssService::refresh(){if(!configured())return;_pending=true;if(_gate){_error="Refresh queued - please wait";++_revision;}}
void IssService::worker(void *ptr){auto*s=static_cast<IssService*>(ptr);
 s->_all_body.clear();s->_visible_body.clear();
 s->_result=request(s->_job_lat,s->_job_lon,false,s->_all_body);
 if(s->_result.error.empty()) {auto visible=request(s->_job_lat,s->_job_lon,true,s->_visible_body);
  if(!visible.error.empty())s->_result=std::move(visible);else iss::combine(s->_result,visible);}
 s->_finished.store(true,std::memory_order_release);
#if defined(ARDUINO)
 vTaskDelete(nullptr);
#endif
}
void IssService::loop(){
 uint32_t ms=now_ms();int64_t now=time(nullptr);
 if(_running&&_finished.load(std::memory_order_acquire)){
  _running=false;uint32_t wait=60000;
  if(!_result.error.empty()){wait=std::max(_backoff,_result.retry_ms);_backoff=std::min<uint32_t>(_backoff*2,1800000);}
  else _backoff=60000;
  _allowed=ms+wait;_gate=true;
  if(_job_key==_key){
   _error=_result.error;
   if(_error.empty()){
    // An API refresh can omit a pass already in progress; retain that pass until set.
    for(const auto&p:_forecast.passes)if(p.rise<=now&&p.set>now){iss::Forecast active;active.passes.push_back(p);iss::combine(_result,active);}
    _forecast=std::move(_result);_fetched=now;
    auto &fs=FileSystemService::instance();if(fs.begin()){
     JsonDocument doc;doc["version"]=1;doc["key"]=_key;doc["fetched"]=_fetched;doc["all"]=_all_body;doc["visible"]=_visible_body;
     std::string raw;serializeJson(doc,raw);fs.fs().mkdir_p("/cache");fs.fs().write(CachePath,raw);
    }
   }else _pending=true;
   ++_revision;
  }
  _result={};_all_body.clear();_visible_body.clear();
 }
 if(_gate&&int32_t(ms-_allowed)>=0)_gate=false;
 if(!_active||!configured()||_running||now<1600000000)return;
#if defined(ARDUINO)
 if(!WifiService::instance().is_connected()||!TimeService::instance().is_synchronized())return;
 auto update=UpdateService::instance().state();if(update==UpdateService::State::Downloading||update==UpdateService::State::Checking||update==UpdateService::State::ReadyToReboot)return;
#endif
 if(!_fetched||now-_fetched>=RefreshSeconds||_fetched>now)_pending=true;
 if(!_pending||_gate)return;
 _pending=false;_running=true;_job_key=_key;_job_lat=_lat;_job_lon=_lon;_finished.store(false,std::memory_order_relaxed);++_revision;
#if defined(ARDUINO)
 if(xTaskCreate(worker,"iss",12288,this,1,nullptr)!=pdPASS){_result.error="Not enough memory for request";_finished.store(true,std::memory_order_release);}
#else
 try{std::thread(worker,this).detach();}catch(...){_result.error="Cannot start request";_finished.store(true,std::memory_order_release);}
#endif
}
SERVICE_REGISTER(IssService);

#endif
