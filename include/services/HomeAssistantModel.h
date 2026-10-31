#pragma once
#include <string>
#include <vector>
namespace ha {
constexpr size_t MaxPages=4, MaxTiles=7, MaxEntities=256;
struct Tile { std::string id,name,icon; };
struct Page { std::string name; std::vector<Tile> tiles; };
struct Config { std::string url,token,ca; std::vector<Page> pages; };
struct Entity {
 std::string id,name,state,unit;
 int brightness=-1,kelvin=0,min_kelvin=2000,max_kelvin=6500;
 bool dimmable=false,warmth=false,color=false;
 bool available()const{return !state.empty()&&state!="unknown"&&state!="unavailable";}
};
std::string domain(const std::string&);
bool valid_entity(const std::string&);
bool valid_url(const std::string&);
bool parse_config(const std::string&,Config&,std::string&,const Config* previous=nullptr);
std::string config_json(const Config&,bool credentials=false);
bool parse_entity(const std::string&,Entity&,std::string&);
// Commands are allowlisted and only apply to configured, available entities.
bool command(const Config&,const Entity&,const std::string&action,int value,std::string&path,std::string&body);
}
