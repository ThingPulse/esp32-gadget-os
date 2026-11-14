#if GADGET_OS_NATIVE_APP_LOADER || !defined(ARDUINO)
#include "experimental/NativeSettings.h"
#include "services/ConfigStoreService.h"
#include <ArduinoJson.h>
#include <string>
#include <cstring>
#ifdef ARDUINO
#include <LittleFS.h>
#else
#include <fstream>
#include <sys/stat.h>
#include <cstdio>
#endif
namespace native_app { namespace settings {
namespace {
std::string path,config_path;
bool name(const char *s,unsigned limit,bool id=false){if(!s || !*s || strlen(s)>limit)return false;for(;*s;++s)if(!(*s>='a'&&*s<='z')&&!(*s>='0'&&*s<='9')&&*s!=(id?'-':'_'))return false;return true;}
bool load(JsonDocument &doc){
 if(path.empty())return false;
#ifdef ARDUINO
 auto f=LittleFS.open(path.c_str(),"r");if(!f){doc.to<JsonObject>();return true;}if(f.size()>2400)return false;return !deserializeJson(doc,f)&&doc.is<JsonObject>();
#else
 std::ifstream f(path);if(!f){doc.to<JsonObject>();return true;}std::string s;char c;while(f.get(c)){if(s.size()>=2400)return false;s+=c;}return !deserializeJson(doc,s)&&doc.is<JsonObject>();
#endif
}
}
// Migration copies legacy data only after the package owns a durable document.
// The marker stays in the legacy domain after uninstall, preventing resurrection.
static void import_legacy(const char *id){
 std::string domain=id;auto suffix=domain.rfind("-preview");
 if(suffix!=std::string::npos&&suffix+8==domain.size())domain.resize(suffix);
 if(domain!="transit"&&domain!="planes"&&domain!="home-assistant")return;
#ifdef ARDUINO
 if(LittleFS.exists(config_path.c_str()))return;
#else
 {std::ifstream f(config_path);if(f)return;}
#endif
 auto&cfg=ConfigStoreService::instance();if(!cfg.begin()||!cfg.load(domain.c_str())||cfg.get_bool("package_config_migrated",false))return;
 JsonDocument d;std::string raw;
 if(domain=="transit"){
  if(cfg.get("provider","transitous")!="transitous")return;
  raw=cfg.get("favourites","[]");JsonDocument stops;if(raw.size()>8192||deserializeJson(stops,raw)||!stops.is<JsonArray>())return;
  d["favourites"]=stops.as<JsonArrayConst>();serializeJson(d,raw=std::string());
 }else if(domain=="home-assistant"){
  raw=cfg.get("configuration","");if(raw.empty())return;
 }else{
  for(auto key:{"center_latitude","center_longitude","range_km"})d[key]=cfg.get(key,"");
  d["metric"]=cfg.get_bool("metric",true);d["hide_ground"]=cfg.get_bool("hide_ground",false);
  serializeJson(d,raw);
 }
 if(config_write(raw.c_str(),raw.size())==0&&cfg.load(domain.c_str())){cfg.set("package_config_migrated",true);cfg.save();}
}
int32_t config_read(char*out,uint32_t cap){
 if(config_path.empty()||!out||cap<1||cap>24577)return -1;
#ifdef ARDUINO
 auto f=LittleFS.open(config_path.c_str(),"r");if(!f||f.size()>=cap||f.size()>24576)return -1;
 size_t n=f.size();if(f.read((uint8_t*)out,n)!=n)return -1;
#else
 std::ifstream f(config_path,std::ios::binary|std::ios::ate);if(!f||f.tellg()<0||f.tellg()>=cap||f.tellg()>24576)return -1;
 size_t n=f.tellg();f.seekg(0);f.read(out,n);if(!f)return -1;
#endif
 out[n]=0;return n;
}
int32_t config_write(const char*data,uint32_t n){
 if(config_path.empty()||!data||!n||n>24576||memchr(data,0,n))return -1;
 JsonDocument d;if(deserializeJson(d,data,n)||!d.is<JsonObject>())return -1;
 std::string tmp=config_path+".tmp";
#ifdef ARDUINO
 auto f=LittleFS.open(tmp.c_str(),"w");if(!f)return -1;auto written=f.write((const uint8_t*)data,n);f.close();
 if(written!=n){LittleFS.remove(tmp.c_str());return -1;}return LittleFS.rename(tmp.c_str(),config_path.c_str())?0:-1;
#else
 {std::ofstream f(tmp,std::ios::binary);if(!f)return -1;chmod(tmp.c_str(),0600);f.write(data,n);f.close();if(!f)return -1;}
 return std::rename(tmp.c_str(),config_path.c_str())==0?0:-1;
#endif
}
void erase_config(const char*id){
 if(!name(id,32,true))return;
#ifdef ARDUINO
 std::string f=std::string("/apps/settings/")+id+".config.json";LittleFS.remove(f.c_str());LittleFS.remove((f+".tmp").c_str());
#else
 std::string f=std::string(".pio/native-app-settings/")+id+".config.json";std::remove(f.c_str());std::remove((f+".tmp").c_str());
#endif
}
void begin(const char *id){path.clear();config_path.clear();if(!name(id,32,true))return;
#ifdef ARDUINO
 LittleFS.mkdir("/apps/settings");path="/apps/settings/";
#else
 mkdir(".pio",0700);mkdir(".pio/native-app-settings",0700);path=".pio/native-app-settings/";
#endif
 config_path=path+std::string(id)+".config.json";
 path+=std::string(id)+".json";
 import_legacy(id);
 // The sole legacy import bridge. Never expose arbitrary host config to apps,
 // replace existing app data, or erase the original watchlist. The legacy
 // marker survives uninstall, so deleted app settings do not resurrect later.
 if(!strcmp(id,"stocks")||!strcmp(id,"stocks-preview")){
  JsonDocument doc;if(!load(doc)||!doc["symbols"].isNull())return;
  auto &cfg=ConfigStoreService::instance();
  if(!cfg.begin()||!cfg.load("stocks")||cfg.get_bool("package_watchlist_migrated",false))return;
  auto symbols=cfg.get("symbols","AAPL\nNESN.SW\n7203.T\nSAP.DE\nVOD.L");
  if(cfg.get("provider","yahoo")!="yahoo"||symbols.size()>255){set("import_error","Legacy watchlist needs review");return;}
  if(set("symbols",symbols.c_str())==0){
   if(cfg.load("stocks")){cfg.set("package_watchlist_migrated",true);cfg.save();}
  }
 }
}
void end(){path.clear();config_path.clear();}
int32_t get(const char *key,char *value,uint32_t cap){if(!name(key,24)||!value||cap<1||cap>256)return -1;JsonDocument doc;if(!load(doc)||!doc[key].is<const char*>())return -1;const char *v=doc[key];size_t n=strlen(v);if(n>=cap)return -1;memcpy(value,v,n+1);return (int32_t)n;}
int32_t set(const char *key,const char *value){if(!name(key,24)||!value||strlen(value)>255)return -1;JsonDocument doc;if(!load(doc))return -1;auto obj=doc.as<JsonObject>();if(obj.size()>=8&&obj[key].isNull())return -1;if(obj[key].is<const char*>()&&!strcmp(obj[key],value))return 0;doc[key]=value;std::string data;serializeJson(doc,data);if(data.size()>2400)return -1;std::string tmp=path+".tmp";
#ifdef ARDUINO
 auto f=LittleFS.open(tmp.c_str(),"w");if(!f)return -1;auto written=f.write((const uint8_t*)data.data(),data.size());f.close();if(written!=data.size()){LittleFS.remove(tmp.c_str());return -1;}return LittleFS.rename(tmp.c_str(),path.c_str())?0:-1;
#else
 {std::ofstream f(tmp,std::ios::binary);if(!f)return -1;chmod(tmp.c_str(),0600);f<<data;f.close();if(!f)return -1;}return std::rename(tmp.c_str(),path.c_str())==0?0:-1;
#endif
}
} }
#endif
