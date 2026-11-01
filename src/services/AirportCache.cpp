#include "services/AirportCache.h"
#include "services/FileSystemService.h"
#include <ArduinoJson.h>
#include <algorithm>
#include <cmath>

namespace {
constexpr const char *path = "/cache/plane-airports.json";
bool code_valid(const std::string &s, size_t min, size_t max) {
    if(s.size()<min || s.size()>max) return false;
    for(char c:s) if(!((c>='A'&&c<='Z')||(c>='0'&&c<='9'))) return false;
    return true;
}
}
bool AirportCache::valid(const AirportLocation &a) {
    return code_valid(a.icao,4,4) && code_valid(a.code,3,4) &&
        std::isfinite(a.latitude) && std::isfinite(a.longitude) &&
        std::abs(a.latitude)<=90 && std::abs(a.longitude)<=180;
}
void AirportCache::clear() {
    entries_.clear();
    selected_=false;
    auto &service=FileSystemService::instance();
    if(service.begin()) service.fs().remove(path);
}
double AirportCache::distance(double lat,double lon,double other_lat,double other_lon) {
    constexpr double rad=3.14159265358979323846/180;
    double a=std::pow(std::sin((other_lat-lat)*rad/2),2)+
        std::cos(lat*rad)*std::cos(other_lat*rad)*std::pow(std::sin((other_lon-lon)*rad/2),2);
    return 12742*std::asin(std::sqrt(std::min(1.0,std::max(0.0,a))));
}
void AirportCache::select_view(double lat,double lon,double range) {
    if(!std::isfinite(lat)||!std::isfinite(lon)||!std::isfinite(range)||std::abs(lat)>90||std::abs(lon)>180||range<=0) return;
    if(selected_ && lat==lat_ && lon==lon_ && range==range_) return;
    const bool first=!selected_;
    selected_=true;lat_=lat;lon_=lon;range_=range;entries_.clear();
    auto &service=FileSystemService::instance();
    if(!service.begin()) return;
    auto &fs=service.fs();
    std::string raw;
    JsonDocument doc;
    if(first && fs.read(path,raw) && raw.size()<=16384 && !deserializeJson(doc,raw) &&
       doc["version"].as<int>()==1 && doc["lat"].is<double>() && doc["lon"].is<double>() && doc["range"].is<double>() &&
       std::abs(doc["lat"].as<double>()-lat)<0.00002 && std::abs(doc["lon"].as<double>()-lon)<0.00002 &&
       std::abs(doc["range"].as<double>()-range)<0.0001 &&
       doc["airports"].is<JsonArray>() && doc["airports"].size()<=LIMIT) {
        for(JsonObjectConst item:doc["airports"].as<JsonArrayConst>()) {
            if(!item["lat"].is<double>()||!item["lon"].is<double>()) continue;
            AirportLocation a{item["icao"]|"",item["code"]|"",item["lat"].as<double>(),item["lon"].as<double>()};
            if(valid(a) && distance(lat,lon,a.latitude,a.longitude)<=range &&
               std::none_of(entries_.begin(),entries_.end(),[&](const AirportLocation &b){return a.icao==b.icao;})) entries_.push_back(a);
        }
    } else fs.remove(path); // Changed view or invalid snapshot: never accumulate per-view files.
}
bool AirportCache::merge(const std::vector<AirportLocation> &airports) {
    if(!selected_) return false;
    bool changed=false;
    for(const auto &a:airports) {
        if(!valid(a)||distance(lat_,lon_,a.latitude,a.longitude)>range_) continue;
        auto it=std::find_if(entries_.begin(),entries_.end(),[&](const AirportLocation &b){return b.icao==a.icao;});
        if(it!=entries_.end()) {
            if(it->code==a.code && it->latitude==a.latitude && it->longitude==a.longitude) continue;
            *it=a;
        } else {
            if(entries_.size()>=LIMIT) continue;
            entries_.push_back(a);
        }
        changed=true;
    }
    if(!changed) return false;
    auto &service=FileSystemService::instance();
    if(service.begin()) {
        JsonDocument doc;
        doc["version"]=1;doc["lat"]=lat_;doc["lon"]=lon_;doc["range"]=range_;
        auto array=doc["airports"].to<JsonArray>();
        for(const auto &a:entries_) {
            auto item=array.add<JsonObject>();item["icao"]=a.icao;item["code"]=a.code;item["lat"]=a.latitude;item["lon"]=a.longitude;
        }
        std::string raw;serializeJson(doc,raw);
        service.fs().mkdir_p("/cache");
        service.fs().write(path,raw); // At most once per changed response batch, never on redraw.
    }
    return true;
}
