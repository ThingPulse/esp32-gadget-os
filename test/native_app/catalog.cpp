#include "experimental/NativeCatalog.h"
#include <ArduinoJson.h>
#include <cassert>
#include <iostream>
using namespace native_app;
int main(){
 const std::string base="https://thingpulse.github.io/esp32-gadget-os/apps/catalog.json";
 const std::string good="https://thingpulse.github.io/esp32-gadget-os/apps/packages/native-demo/0.1.0/esp32.gapp";
 assert(catalog::allowed_url(good,base));assert(catalog::allowed_url(base,base));
 for(const auto &bad:{"http://thingpulse.github.io/esp32-gadget-os/apps/x","https://evil.test/apps/x","https://thingpulse.github.io/esp32-gadget-os/apps/../private","https://thingpulse.github.io/esp32-gadget-os/apps/%2e%2e/private","https://thingpulse.github.io/esp32-gadget-os/apps/x#fragment","https://thingpulse.github.io/esp32-gadget-os/apps/x?token=1","https://thingpulse.github.io/esp32-gadget-os/apps/..","https://thingpulse.github.io/esp32-gadget-os/apps/./x"})assert(!catalog::allowed_url(bad,base));
 JsonDocument d;d["schema"]=1;auto a=d["apps"].to<JsonArray>().add<JsonObject>();
 a["id"]="native-demo";a["name"]="Counter";a["version"]="0.1.0";a["chip"]="esp32";a["api_major"]=1;a["api_minor"]=0;a["bytes"]=4000;a["url"]=good;
 std::vector<catalog::Item> items;std::string error;
 auto parse=[&](const char *chip="esp32"){std::string json;serializeJson(d,json);return catalog::parse(json.data(),json.size(),chip,base,items,error);};
 assert(parse()&&items.size()==1&&!items[0].description.empty());assert(parse("esp32s3")&&items.empty());
 a["api_minor"]=2;assert(parse()&&items.size()==1);a["api_minor"]=3;assert(parse()&&items.size()==1);a["api_minor"]=4;assert(parse()&&items.size()==1);a["api_minor"]=5;assert(parse()&&items.size()==1);a["api_minor"]=6;assert(parse()&&items.size()==1);a["api_minor"]=7;assert(parse()&&items.size()==1);a["api_minor"]=8;assert(parse()&&items.size()==1);a["api_minor"]=9;assert(parse()&&items.size()==1);a["api_minor"]=10;assert(parse()&&items.size()==1);a["api_minor"]=11;assert(parse()&&items.size()==1);a["api_minor"]=12;assert(parse()&&items.empty());a["api_minor"]=-1;assert(!parse());a["api_minor"]=0;
 a["url"]="https://evil.test/x";assert(!parse()&&items.empty());a["url"]=good;
 a["bytes"]=80000;assert(!parse());a["bytes"]=4000;
 a["description"]=std::string(241,'x');assert(!parse());a["description"]="Count things.";assert(parse());
 auto copy=d["apps"].as<JsonArray>().add<JsonObject>();copy.set(a);assert(!parse());
 assert(!catalog::parse("{}",2,"esp32",base,items,error));
 assert(!catalog::parse("{}",65537,"esp32",base,items,error));
 std::cout<<"PASS: device catalog compatibility, bounds, duplicates and URL confinement\n";
}
