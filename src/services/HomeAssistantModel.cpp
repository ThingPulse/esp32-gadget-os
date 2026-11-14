#if !GADGET_OS_NATIVE_APP_LOADER && !GADGET_OS_APP_STORE_SIMULATOR
#include "services/HomeAssistantModel.h"
#include <ArduinoJson.h>
#include <algorithm>
#include <cctype>

namespace ha {
std::string domain(const std::string&id){return id.substr(0,id.find('.'));}
bool valid_entity(const std::string&id){auto d=domain(id);return id.size()<=128&&id.size()>d.size()+1&&id.find('.',d.size()+1)==std::string::npos&&id.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789_.")==std::string::npos&&(d=="light"||d=="switch"||d=="sensor"||d=="binary_sensor"||d=="scene"||d=="script");}
bool valid_url(const std::string&url){
 size_t start=url.compare(0,7,"http://")==0?7:url.compare(0,8,"https://")==0?8:0;
 if(!start||url.size()>240||url.size()<=start)return false;
 auto host=url.substr(start); // Base origin only: no credentials, query, path or fragments.
 if(host.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789.-:[]")!=std::string::npos)return false;
 if(host.front()==':'||host.front()=='.')return false;
 return true;
}
bool parse_config(const std::string&raw,Config&out,std::string&error,const Config*prev){
 auto fail=[&](const char*s){error=s;return false;};
 if(raw.size()>24576)return fail("Configuration too large");
 JsonDocument d;if(deserializeJson(d,raw)||!d.is<JsonObject>())return fail("Invalid configuration");
 Config c;c.url=d["url"]|"";while(!c.url.empty()&&c.url.back()=='/')c.url.pop_back();
 c.token=d["token"]|"";c.ca=d["ca"]|"";
 if(prev&&c.token.empty()&&c.url==prev->url)c.token=prev->token;
 if(!valid_url(c.url))return fail("Enter an http:// or https:// base address");
 if(c.token.empty()||c.token.size()>4096||c.token.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789._-=")!=std::string::npos)return fail("Invalid access token");
 if(c.ca.size()>8192||(!c.ca.empty()&&c.ca.find("-----BEGIN CERTIFICATE-----")==std::string::npos))return fail("Invalid CA certificate");
 if(!d["pages"].is<JsonArray>()||d["pages"].size()>MaxPages)return fail("Maximum 4 pages");
 for(JsonObjectConst p:d["pages"].as<JsonArrayConst>()){
  Page page;page.name=p["name"]|"";
  if(page.name.empty()||page.name.size()>48||!p["tiles"].is<JsonArrayConst>()||p["tiles"].size()>MaxTiles)return fail("Each page needs a name and at most 7 tiles");
  for(JsonObjectConst t:p["tiles"].as<JsonArrayConst>()){
   Tile tile{t["id"]|"",t["name"]|"",t["icon"]|"auto"};
   if(!valid_entity(tile.id)||tile.name.size()>64||std::any_of(page.tiles.begin(),page.tiles.end(),[&](const Tile&v){return v.id==tile.id;}))return fail("Invalid or duplicate tile");
   if(tile.icon!="auto"&&tile.icon!="light"&&tile.icon!="power"&&tile.icon!="temperature"&&tile.icon!="water"&&tile.icon!="play"&&tile.icon!="energy")return fail("Invalid icon");
   page.tiles.push_back(tile);
  }c.pages.push_back(page);
 }
 out=std::move(c);error.clear();return true;
}
std::string config_json(const Config&c,bool secret){JsonDocument d;d["url"]=c.url;d["ca"]=c.ca;if(secret)d["token"]=c.token;else d["has_token"]=!c.token.empty();auto pages=d["pages"].to<JsonArray>();for(auto&p:c.pages){auto o=pages.add<JsonObject>();o["name"]=p.name;auto tiles=o["tiles"].to<JsonArray>();for(auto&t:p.tiles){auto v=tiles.add<JsonObject>();v["id"]=t.id;v["name"]=t.name;v["icon"]=t.icon;}}std::string s;serializeJson(d,s);return s;}
namespace {
void entity_filter(JsonObject f){f["entity_id"]=true;f["state"]=true;for(auto k:{"friendly_name","unit_of_measurement","brightness","color_temp_kelvin","min_color_temp_kelvin","max_color_temp_kelvin","supported_color_modes"})f["attributes"][k]=true;}
Entity decode_entity(JsonObjectConst o){
 Entity e;e.id=o["entity_id"]|"";JsonObjectConst a=o["attributes"];e.name=a["friendly_name"]|e.id.c_str();e.state=o["state"]|"";e.unit=a["unit_of_measurement"]|"";
 if(e.name.size()>128)e.name.resize(128);if(e.state.size()>128)e.state.resize(128);if(e.unit.size()>24)e.unit.resize(24);
 e.brightness=a["brightness"]|-1;e.kelvin=a["color_temp_kelvin"]|0;e.min_kelvin=a["min_color_temp_kelvin"]|2000;e.max_kelvin=a["max_color_temp_kelvin"]|6500;
 e.min_kelvin=std::max(1000,std::min(10000,e.min_kelvin));e.max_kelvin=std::max(e.min_kelvin,std::min(10000,e.max_kelvin));
 for(JsonVariantConst m:a["supported_color_modes"].as<JsonArrayConst>()){std::string mode=m|"";if(mode!="onoff"&&mode!="unknown"&&!mode.empty())e.dimmable=true;if(mode=="color_temp")e.warmth=true;if(mode=="hs"||mode=="xy"||mode=="rgb"||mode=="rgbw"||mode=="rgbww")e.color=true;}
 return e;
}
}
bool parse_entity(const std::string&raw,Entity&out,std::string&error){
 JsonDocument filter;entity_filter(filter.to<JsonObject>());JsonDocument d;
 if(raw.size()>1048576||deserializeJson(d,raw,DeserializationOption::Filter(filter),DeserializationOption::NestingLimit(32))||!d.is<JsonObject>()){error="Invalid Home Assistant entity";return false;}
 out=decode_entity(d.as<JsonObjectConst>());return true;
}
bool command(const Config&c,const Entity&e,const std::string&action,int value,std::string&path,std::string&body){
 bool selected=false;for(auto&p:c.pages)for(auto&t:p.tiles)if(t.id==e.id)selected=true;
 if(!selected||!e.available()||!valid_entity(e.id))return false;
 auto kind=domain(e.id);std::string service;JsonDocument d;d["entity_id"]=e.id;
 if((kind=="light"||kind=="switch")&&action=="toggle")service=e.state=="on"?"turn_off":"turn_on";
 else if((kind=="scene"||kind=="script")&&action=="activate")service="turn_on";
 else if(kind=="light"&&action=="brightness"&&e.dimmable&&value>=0&&value<=100){service="turn_on";d["brightness_pct"]=value;}
 else if(kind=="light"&&action=="warmth"&&e.warmth&&value>=e.min_kelvin&&value<=e.max_kelvin){service="turn_on";d["color_temp_kelvin"]=value;}
 else if(kind=="light"&&action=="color"&&e.color&&value>=0&&value<=0xffffff){service="turn_on";auto rgb=d["rgb_color"].to<JsonArray>();rgb.add((value>>16)&255);rgb.add((value>>8)&255);rgb.add(value&255);}
 else return false;
 path="/api/services/"+kind+"/"+service;body.clear();serializeJson(d,body);return true;
}
}

#endif
