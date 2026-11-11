#include "experimental/CompanionChannels.h"
#include <ArduinoJson.h>
#include <cstring>

namespace native_app::companion {
bool channel_name_valid(const std::string &name){
    if(name.empty()||name.size()>32||name[0]<'a'||name[0]>'z')return false;
    for(char c:name)if(!((c>='a'&&c<='z')||(c>='0'&&c<='9')||c=='-'))return false;
    return true;
}
bool channel_route(const std::string &route,std::string &app,std::string &channel){
    const std::string prefix="/v1/apps/";
    if(route.compare(0,prefix.size(),prefix)!=0)return false;
    auto split=route.find('/',prefix.size());if(split==std::string::npos)return false;
    auto a=route.substr(prefix.size(),split-prefix.size()),c=route.substr(split+1);
    if(!channel_name_valid(a)||!channel_name_valid(c))return false;
    app=std::move(a);channel=std::move(c);return true;
}
namespace {
bool json_object(const char *data,size_t size){
    if(!data||!size||size>channel_payload_limit||std::memchr(data,0,size))return false;
    // ArduinoJson permits trailing input. Require exactly one complete object.
    size_t start=0,end=size;
    auto space=[](char c){return c==' '||c=='\t'||c=='\r'||c=='\n';};
    while(start<end&&space(data[start]))++start;
    while(end>start&&space(data[end-1]))--end;
    if(start==end||data[start]!='{'||data[end-1]!='}')return false;
    unsigned depth=0;bool quoted=false,escaped=false;
    for(size_t i=start;i<end;++i){char c=data[i];
        if(quoted){if(escaped)escaped=false;else if(c=='\\')escaped=true;else if(c=='"')quoted=false;continue;}
        if(c=='"')quoted=true;
        else if(c=='{'||c=='['){if(++depth>8)return false;}
        else if(c=='}'||c==']'){if(!depth||(--depth==0&&i+1!=end))return false;}
    }
    if(quoted||depth)return false;
    JsonDocument doc;
    return !deserializeJson(doc,data,size,DeserializationOption::NestingLimit(8))&&doc.is<JsonObject>();
}
}
int ChannelStore::publish(const std::string &app,const std::string &channel,const char *data,size_t size,uint64_t now){
    if(!channel_name_valid(app)||!channel_name_valid(channel)||!json_object(data,size))return 400;
    Entry *slot=nullptr;
    for(auto &entry:entries)if(entry.app==app&&entry.channel==channel){
        if(!entry.payload.empty()&&now>=entry.received&&now-entry.received<500)return 429;
        slot=&entry;break;
    }
    if(!slot)for(auto &entry:entries)if(entry.payload.empty()){slot=&entry;break;}
    // A bounded cache: evict the oldest publication when all slots are occupied.
    if(!slot){slot=&entries[0];for(auto &entry:entries)if(entry.received<slot->received)slot=&entry;}
    slot->app=app;slot->channel=channel;slot->payload.assign(data,size);slot->received=now;return 200;
}
int ChannelStore::read(const std::string &app,const std::string &channel,char *out,uint32_t capacity,uint32_t *age_ms,uint64_t now)const{
    if(!out||!age_ms||!channel_name_valid(app)||!channel_name_valid(channel))return -1;
    for(const auto &entry:entries)if(entry.app==app&&entry.channel==channel&&!entry.payload.empty()){
        if(now<entry.received||now-entry.received>=channel_max_age_ms||entry.payload.size()>=capacity)return -1;
        std::memcpy(out,entry.payload.data(),entry.payload.size());out[entry.payload.size()]=0;
        *age_ms=static_cast<uint32_t>(now-entry.received);return static_cast<int>(entry.payload.size());
    }
    return -1;
}
void ChannelStore::clear(){for(auto &entry:entries)entry=Entry{};}
}
