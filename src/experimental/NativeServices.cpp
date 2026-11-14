#if GADGET_OS_NATIVE_APP_LOADER || !defined(ARDUINO)
#include "experimental/NativeServices.h"
#include "services/WeatherLocationStore.h"
#include "services/ConfigStoreService.h"
#include "services/TimeService.h"
#include "services/WifiService.h"
#include "services/TransitProvider.h"
#include "ui/I18n.h"
#include <ArduinoJson.h>
#include <cstring>
#include <cmath>
#include <ctime>
#include <cstdio>
#include <atomic>
#ifdef ARDUINO
#include <esp_heap_caps.h>
#endif
#ifdef ARDUINO
#include <LittleFS.h>
#include <freertos/task.h>
#else
#include <fstream>
#include <sys/stat.h>
#endif
namespace native_app::services {
namespace {
#ifdef ARDUINO
struct JsonAllocator:ArduinoJson::Allocator {
 void* allocate(size_t n) override{return heap_caps_malloc(n,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);}
 void deallocate(void*p) override{heap_caps_free(p);}
 void* reallocate(void*p,size_t n) override{return heap_caps_realloc(p,n,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);}
} allocator;
JsonDocument document(&allocator);
#else
JsonDocument document;
#endif
std::atomic<bool> cache_busy{false},cache_cancelled{false};std::string write_path;
std::string cache_path;bool opened=false,location_loaded=false;gadget_environment location_cache{};uint32_t location_checked=0;
JsonVariantConst at(const char*path){if(!opened||!path||strlen(path)>128)return {};JsonVariantConst v=document.as<JsonVariantConst>();const char*p=path;unsigned depth=0;while(*p){if(++depth>8)return {};char key[49];unsigned n=0;while(*p&&*p!='/'){if(n==48)return {};key[n++]=*p++;}key[n]=0;if(*p)++p;if(!n)return {};if(v.is<JsonArrayConst>()){unsigned i=0;for(unsigned j=0;j<n;++j){if(key[j]<'0'||key[j]>'9'||i>1000)return {};i=i*10+key[j]-'0';}v=v[i];}else v=v[key];}return v;}
int copy(const std::string&s,char*out,unsigned cap){if(!out||!cap||cap>8193||s.size()>=cap)return -1;memcpy(out,s.c_str(),s.size()+1);return s.size();}
}
void begin(const char*id){end();if(!id||!*id||strlen(id)>32)return;for(const char*p=id;*p;++p)if(!(*p>='a'&&*p<='z')&&!(*p>='0'&&*p<='9')&&*p!='-')return;
#ifdef ARDUINO
 LittleFS.mkdir("/apps/cache");cache_path="/apps/cache/";
#else
 mkdir(".pio",0700);mkdir(".pio/native-app-cache",0700);cache_path=".pio/native-app-cache/";
#endif
 cache_path+=std::string(id)+".bin";
}
void end(){json_close();cache_path.clear();location_loaded=false;}
int32_t environment(gadget_environment*out){if(!out||out->struct_size<sizeof(*out))return -1;*out={sizeof(*out)};out->epoch=std::time(nullptr);out->clock_ready=out->epoch>=1600000000;out->german=i18n::german();out->use24=TimeService::instance().use_24h();out->online=1;
#ifdef ARDUINO
 out->online=WifiService::instance().is_connected();out->clock_ready=TimeService::instance().is_synchronized();
#endif
 if(location_loaded&&out->epoch>=location_checked&&out->epoch-location_checked<30){out->has_location=location_cache.has_location;out->latitude_e6=location_cache.latitude_e6;out->longitude_e6=location_cache.longitude_e6;memcpy(out->location,location_cache.location,sizeof(out->location));return 0;}
 WeatherLocation loc;bool found=WeatherLocationStore::active(loc);
 if(!found){auto&cfg=ConfigStoreService::instance();if(cfg.begin()&&cfg.load("planes")){auto lat=cfg.get("center_latitude","");auto lon=cfg.get("center_longitude","");if(!lat.empty()&&!lon.empty()){loc.latitude=strtod(lat.c_str(),nullptr);loc.longitude=strtod(lon.c_str(),nullptr);loc.name=i18n::tr("Your location");found=true;}}}
 if(found&&std::isfinite(loc.latitude)&&std::isfinite(loc.longitude)&&fabs(loc.latitude)<=90&&fabs(loc.longitude)<=180){out->has_location=1;out->latitude_e6=lround(loc.latitude*1000000);out->longitude_e6=lround(loc.longitude*1000000);snprintf(out->location,sizeof(out->location),"%s",loc.name.c_str());}location_cache=*out;location_checked=out->epoch;location_loaded=true;return 0;
}
uint32_t parse_time(const char*s){if(!s||strlen(s)>40)return 0;auto t=transit::timestamp(s);return t>0&&t<=UINT32_MAX?uint32_t(t):0;}
int32_t format_time(uint32_t stamp,uint32_t mode,char*out,uint32_t cap){if(mode>2)return -1;
 if(mode==2){time_t t=stamp;tm utc{};gmtime_r(&t,&utc);char result[40];strftime(result,sizeof(result),i18n::german()?"%d.%m. %H:%M UTC":"%d %b %H:%M UTC",&utc);return copy(result,out,cap);}
 time_t t=stamp,now=time(nullptr);tm a{},b{};localtime_r(&t,&a);localtime_r(&now,&b);char clock[24];strftime(clock,sizeof(clock),TimeService::instance().use_24h()?"%H:%M":"%I:%M %p",&a);if(!mode)return copy(clock,out,cap);char date[40];if(a.tm_year==b.tm_year&&a.tm_yday==b.tm_yday)snprintf(date,sizeof(date),"%s",i18n::tr("Today"));else i18n::date(date,sizeof(date),a);return copy(std::string(date)+" - "+clock,out,cap);}
int32_t json_open(const char*s,uint32_t n){json_close();if(!s||!n||n>196608||deserializeJson(document,s,n,DeserializationOption::NestingLimit(16))){json_close();return -1;}opened=true;return 0;}
int32_t json_get(const char*path,char*out,uint32_t cap){auto v=at(path);if(v.isNull()||v.is<JsonObjectConst>()||v.is<JsonArrayConst>())return -1;if(v.is<const char*>())return copy(v.as<const char*>(),out,cap);std::string s;serializeJson(v,s);return copy(s,out,cap);}
int32_t json_count(const char*p){auto v=at(p);return v.is<JsonArrayConst>()?int32_t(v.size()):-1;}
int32_t json_number(const char*p,int32_t scale,int32_t*out){auto v=at(p);if(!out||scale<1||scale>1000000||!v.is<double>())return -1;double n=std::round(v.as<double>()*scale);if(!std::isfinite(n)||n<INT32_MIN||n>INT32_MAX)return -1;*out=int32_t(n);return 0;}
int32_t json_real(const char*p,double*out){auto v=at(p);if(!out||!v.is<double>())return -1;double n=v.as<double>();if(!std::isfinite(n))return -1;*out=n;return 0;}
int32_t format_number(double n,uint32_t decimals,char*out,uint32_t cap){if(!out||!cap||cap>160||decimals>8||!std::isfinite(n)||fabs(n)>1e15)return -1;int written=snprintf(out,cap,"%.*f",int(decimals),n);if(written<0||unsigned(written)>=cap){out[0]=0;return -1;}return written;}
uint32_t monotonic_ms(){return lv_tick_get();}
const gadget_math_api math_api={sizeof(gadget_math_api),
 [](uint32_t op,double a,double b)->double{switch(op){case 0:return a+b;case 1:return a-b;case 2:return a*b;case 3:return b==0?NAN:a/b;case 4:return atan2(a,b);default:return NAN;}},
 [](double a,double b)->int32_t{return std::isnan(a)||std::isnan(b)?2:a<b?-1:a>b?1:0;},
 [](int32_t n)->double{return n;},[](uint32_t n)->double{return n;},
 [](double n)->int32_t{return !std::isfinite(n)?0:n<INT32_MIN?INT32_MIN:n>INT32_MAX?INT32_MAX:int32_t(n);},
 [](double n)->uint32_t{return !std::isfinite(n)||n<0?0:n>UINT32_MAX?UINT32_MAX:uint32_t(n);},
 [](uint32_t op,double n)->double{switch(op){case 0:return sin(n);case 1:return cos(n);case 2:return n<0?NAN:sqrt(n);default:return NAN;}}};
void json_close(){opened=false;document.clear();document.shrinkToFit();}
int32_t cache_read(void*out,uint32_t cap){if(cache_path.empty()||!out||!cap||cap>16384)return -1;
#ifdef ARDUINO
 auto f=LittleFS.open(cache_path.c_str(),"r");if(!f||f.size()>cap||f.size()>16384)return -1;size_t n=f.size();return f.read((uint8_t*)out,n)==n?int(n):-1;
#else
 std::ifstream f(cache_path,std::ios::binary|std::ios::ate);if(!f||f.tellg()>cap)return -1;size_t n=f.tellg();f.seekg(0);f.read((char*)out,n);return f?int(n):-1;
#endif
}
void erase_cache(const char*id){if(!id||!*id||strlen(id)>32)return;for(const char*p=id;*p;++p)if(!(*p>='a'&&*p<='z')&&!(*p>='0'&&*p<='9')&&*p!='-')return;
#ifdef ARDUINO
 std::string path=std::string("/apps/cache/")+id+".bin";if(cache_busy.load()&&write_path==path)cache_cancelled=true;LittleFS.remove(path.c_str());LittleFS.remove((path+".tmp").c_str());
#else
 std::string path=std::string(".pio/native-app-cache/")+id+".bin";std::remove(path.c_str());std::remove((path+".tmp").c_str());
#endif
}
int32_t cache_write(const void*data,uint32_t n){if(cache_path.empty()||!data||!n||n>16384)return -1;
#ifdef ARDUINO
 struct Job {std::string path,bytes;};if(cache_busy.exchange(true))return -1;cache_cancelled=false;write_path=cache_path;auto*j=new(std::nothrow) Job{cache_path,std::string((const char*)data,n)};if(!j){cache_busy=false;return -1;}
 if(xTaskCreate([](void*ptr){auto*j=(Job*)ptr;std::string tmp=j->path+".tmp";auto f=LittleFS.open(tmp.c_str(),"w");bool ok=f&&f.write((const uint8_t*)j->bytes.data(),j->bytes.size())==j->bytes.size();f.close();if(ok&&!cache_cancelled.load())LittleFS.rename(tmp.c_str(),j->path.c_str());else LittleFS.remove(tmp.c_str());if(cache_cancelled.load())LittleFS.remove(j->path.c_str());delete j;cache_busy=false;vTaskDelete(nullptr);},"app-cache",4096,j,1,nullptr)!=pdPASS){delete j;cache_busy=false;return -1;}return 0;
#else
 std::string tmp=cache_path+".tmp";{std::ofstream f(tmp,std::ios::binary);f.write((const char*)data,n);f.close();if(!f)return -1;}return rename(tmp.c_str(),cache_path.c_str())==0?0:-1;
#endif
}
}
#endif
