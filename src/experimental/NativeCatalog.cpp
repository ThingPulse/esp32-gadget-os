#if GADGET_OS_NATIVE_APP_LOADER
#include "experimental/NativeCatalog.h"
#include "experimental/NativePackage.h"
#include <ArduinoJson.h>
#include <algorithm>
namespace native_app::catalog {
bool allowed_url(const std::string &url,const std::string &base) {
    if(base.rfind("https://",0)!=0)return false;
    auto authority_end=base.find('/',8),end=base.rfind('/');
    if(authority_end==std::string::npos||authority_end==8||end<authority_end)return false;
    if(url.compare(0,end+1,base,0,end+1)!=0)return false;
    for(unsigned char c:url)if(c<=32||c>=127||c=='%'||c=='\\'||c=='@'||c=='?'||c=='#')return false;
    if(url.find("/../")!=std::string::npos||url.find("/./")!=std::string::npos)return false;
    return url.size()>end+1 && url.back()!='/' && url.substr(url.size()-2)!="/." &&
           (url.size()<3||url.substr(url.size()-3)!="/..");
}
bool parse(const char *data,size_t size,const char *chip,const std::string &base,
           std::vector<Item> &items,std::string &error) {
    items.clear();error.clear();
    auto fail=[&](const char *why){items.clear();error=why;return false;};
    if(!size||size>65536)return fail("Catalog is missing or too large.");
    JsonDocument d;
    if(deserializeJson(d,data,size)||d["schema"]!=1||!d["apps"].is<JsonArray>()||d["apps"].size()>128)
        return fail("Unsupported app catalog.");
    for(JsonObject a:d["apps"].as<JsonArray>()) {
        if(a["chip"].as<std::string>()!=chip)continue;
        if(!a["api_major"].is<unsigned>()||!a["api_minor"].is<unsigned>())return fail("Invalid app compatibility.");
        if(a["api_major"]!=1||a["api_minor"].as<unsigned>()>5)continue;
        Item item{a["id"]|"",a["name"]|"",a["version"]|"",a["description"]|"",a["url"]|"",a["bytes"]|size_t(0)};
        if(!valid_app_id(item.id)||item.name.empty()||item.name.size()>32||item.version.empty()||item.version.size()>32||
           item.description.size()>240||item.bytes<signed_header_bytes||item.bytes>max_package_bytes||!allowed_url(item.url,base))
            return fail("Invalid catalog entry.");
        for(const auto &s:{item.name,item.version,item.description})for(unsigned char c:s)if(c<32||c==127)return fail("Invalid catalog text.");
        if(std::any_of(items.begin(),items.end(),[&](const Item &i){return i.id==item.id;}))return fail("Duplicate catalog app.");
        if(item.description.empty())item.description="An additional app for Gadget OS.";
        items.push_back(std::move(item));
    }
    return true;
}
}
#endif
