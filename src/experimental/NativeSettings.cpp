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
std::string path;
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
void begin(const char *id){path.clear();if(!name(id,32,true))return;
#ifdef ARDUINO
 LittleFS.mkdir("/apps/settings");path="/apps/settings/";
#else
 mkdir(".pio",0700);mkdir(".pio/native-app-settings",0700);path=".pio/native-app-settings/";
#endif
 path+=std::string(id)+".json";
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
void end(){path.clear();}
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
