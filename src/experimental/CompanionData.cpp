#include "experimental/CompanionData.h"
#include <ArduinoJson.h>
#include <algorithm>
#include <cmath>
#include <cstring>
namespace native_app::companion {
namespace {
bool number(JsonVariantConst v,double low,double high){return v.is<double>()&&!v.is<bool>()&&std::isfinite(v.as<double>())&&v.as<double>()>=low&&v.as<double>()<=high;}
}
bool UsageSnapshot::update(const char *json,size_t size,uint64_t now){
    if(!json||!size||size>2048)return false;
    JsonDocument input,clean;
    if(deserializeJson(input,json,size)||!input.is<JsonObject>()||(!input["schema"].is<int>()||input["schema"].as<int>()!=1))return false;
    clean["schema"]=1;
    for(const char *provider:{"codex","claude"}){
        std::string p=provider;
        if(!number(input[p+"_enabled"],0,1)||!input[p+"_enabled"].is<int>()||!number(input[p+"_age"],-1,604800))return false;
        const int enabled=input[p+"_enabled"],age=input[p+"_age"];
        clean[p+"_enabled"]=enabled;clean[p+"_age"]=age;
        for(const char *window:{"primary","secondary"}){
            auto key=p+"_"+window;
            if(!enabled){clean[key+"_used"]=-1;continue;}
            if(!number(input[key+"_used"],-1,100))return false;
            double used=input[key+"_used"];
            if(used<0&&used!=-1)return false;
            clean[key+"_used"]=used;
            if(used>=0){
                if(age<0||!number(input[key+"_minutes"],1,525600)||!number(input[key+"_reset"],0,31536000))return false;
                clean[key+"_minutes"]=input[key+"_minutes"].as<int>();clean[key+"_reset"]=input[key+"_reset"].as<int>();
            }
        }
    }
    // Optional detail fields are allowlisted and independently bounded. Older
    // companions remain valid; absence is not interpreted as a zero count.
    const auto copy=[&](const std::string &key,int low,int high){
        if(input[key].is<int>()&&number(input[key],low,high))clean[key]=input[key].as<int>();
    };
    copy("codex_detail_age",-1,604800);
    copy("codex_streak",0,36500);copy("codex_best_streak",0,36500);copy("codex_longest_sec",0,31536000);
    const auto count=[&](const std::string &key){
        if(input[key+"_v"].is<int>()&&number(input[key+"_v"],0,9999)&&input[key+"_u"].is<int>()&&number(input[key+"_u"],0,4)){
            clean[key+"_v"]=input[key+"_v"].as<int>();clean[key+"_u"]=input[key+"_u"].as<int>();
        }
    };
    count("codex_lifetime");count("codex_peak");
    for(int i=0;i<7;++i){auto key="codex_d"+std::to_string(i);count(key);copy(key+"_date",101,1231);copy(key+"_bar",0,100);}
    for(int i=0;i<3;++i){auto key="cx_p"+std::to_string(i);count(key);copy(key+"_n",0,31);copy(key+"_e",1,31);}
    for(int i=0;i<7;++i){auto key="cx_m"+std::to_string(i);count(key);copy(key+"_bar",0,100);}
    payload.clear();serializeJson(clean,payload);received_at=now;return true;
}
int UsageSnapshot::read(char *out,uint32_t capacity,uint64_t now)const{
    if(payload.empty()||!out||!capacity)return -1;
    // Discard a week-old snapshot, including if the monotonic clock was reset.
    if(now<received_at)return -1;
    const uint64_t age=(now-received_at)/1000;
    if(age>=604800)return -1;
    JsonDocument doc;deserializeJson(doc,payload);
    for(const char *provider:{"codex","claude"}){
        std::string p=provider;int observed=doc[p+"_age"];
        if(observed>=0)doc[p+"_age"]=std::min(604800,observed+int(age));
        for(const char *window:{"primary","secondary"}){
            auto key=p+"_"+window;
            if(doc[key+"_used"].as<double>()>=0){
                int remaining=doc[key+"_reset"].as<int>()-int(age);
                if(remaining<=0)doc[key+"_used"]=-1;
                else doc[key+"_reset"]=remaining;
            }
        }
    }
    if(doc["codex_detail_age"].is<int>()&&doc["codex_detail_age"].as<int>()>=0)doc["codex_detail_age"]=std::min(604800,doc["codex_detail_age"].as<int>()+int(age));
    const size_t n=measureJson(doc);if(n>=capacity)return -1;
    serializeJson(doc,out,capacity);return int(n);
}
}

namespace native_app::companion {
bool SystemSnapshot::update(const char *s,size_t n,uint64_t now){
 if(!s||!n||n>2048)return false;JsonDocument d,c;if(deserializeJson(d,s,n)||!d.is<JsonObject>()||!d["schema"].is<int>()||d["schema"].as<int>()!=1||!d["age"].is<int>()||d["age"].as<int>()<0||d["age"].as<int>()>3600)return false;
 c["schema"]=1;c["age"]=d["age"].as<int>();
 auto integer=[&](const std::string &k,int max){if(d[k].is<int>()&&d[k].as<int>()>=-1&&d[k].as<int>()<=max)c[k]=d[k].as<int>();else c[k]=-1;};
 auto str=[&](const std::string &k,size_t max,bool hex=false){if(!d[k].is<const char*>())return;std::string v=d[k].as<std::string>();if(v.size()>max)return;for(unsigned char x:v)if(x<32||x>126||(hex&&!((x>='0'&&x<='9')||(x>='a'&&x<='f'))))return;if(hex&&v.size()%2)return;c[k]=v;};
 for(auto k:{"cpu","ram","battery"})integer(k,100);integer("power",3);integer("cores",4096);
 for(auto k:{"mem_mib","total_mib","disk_total_mib","disk_free_mib","down_bps","up_bps","uptime"})integer(k,2147483647);
 str("host",48);str("volume",48);str("cpu_hist",120,true);str("ram_hist",120,true);
 for(auto prefix:{"ct","mt"})for(int i=0;i<5;++i){auto k=std::string(prefix)+std::to_string(i);str(k+"_name",48);integer(k+"_pid",2147483647);integer(k+"_value",prefix[0]=='c'?1000:2147483647);}
 std::string next;serializeJson(c,next);if(next.size()>2048)return false;payload=std::move(next);received_at=now;return true;
}
int SystemSnapshot::read(char *out,uint32_t cap,uint64_t now)const{
 if(payload.empty()||!out||!cap||now<received_at)return -1;JsonDocument d;if(deserializeJson(d,payload))return -1;
 d["age"]=std::min<uint64_t>(2147483647,uint64_t(d["age"].as<int>())+(now-received_at)/1000);
 const size_t n=measureJson(d);if(n>=cap)return -1;serializeJson(d,out,cap);return n;
}
}

namespace native_app::companion {
bool ActivitySnapshot::update(const char *s,size_t n,uint64_t now){
 if(!s||!n||n>2048)return false;
 JsonDocument d,c;if(deserializeJson(d,s,n)||!d.is<JsonObject>())return false;
 auto integer=[&](const std::string &k,int lo,int hi){return d[k].is<int>()&&d[k].as<int>()>=lo&&d[k].as<int>()<=hi;};
 if(!integer("schema",1,1)||!integer("age",0,604800)||!integer("count",0,6)||!integer("total",0,10000)||!integer("attention",0,10000)||!integer("enabled",0,1))return false;
 int count=d["count"];if(d["total"].as<int>()<count||d["attention"].as<int>()>d["total"].as<int>())return false;
 for(auto k:{"schema","age","count","total","attention","enabled"})c[k]=d[k].as<int>();
 for(int i=0;i<count;++i){auto p="s"+std::to_string(i)+"_";
  for(auto k:{"state","provider","elapsed","age"}){std::string key=p+k;int hi=std::strcmp(k,"state")==0?3:std::strcmp(k,"provider")==0?1:604800;if(!integer(key,0,hi))return false;c[key]=d[key].as<int>();}
  if(!d[p+"tokens_v"].isNull()||!d[p+"tokens_u"].isNull()){
   if(!integer(p+"tokens_v",0,9999)||!integer(p+"tokens_u",0,4))return false;
   c[p+"tokens_v"]=d[p+"tokens_v"].as<int>();c[p+"tokens_u"]=d[p+"tokens_u"].as<int>();
  }
  for(auto k:{"id","project","event"}){auto key=p+k;if(!d[key].is<const char*>())return false;std::string v=d[key].as<std::string>();if(v.empty()||v.size()>48)return false;for(unsigned char ch:v)if(ch<32||ch>126)return false;c[key]=v;}
 }
 std::string next;serializeJson(c,next);if(next.size()>2048)return false;payload=std::move(next);received_at=now;return true;
}
int ActivitySnapshot::read(char *out,uint32_t cap,uint64_t now)const{
 if(payload.empty()||!out||!cap||now<received_at)return -1;
 JsonDocument d;if(deserializeJson(d,payload))return -1;
 unsigned delta=(unsigned)std::min<uint64_t>(604800,(now-received_at)/1000);
 d["age"]=std::min(604800,d["age"].as<int>()+(int)delta);
 for(int i=0;i<d["count"].as<int>();++i){auto p="s"+std::to_string(i)+"_";d[p+"age"]=std::min(604800,d[p+"age"].as<int>()+(int)delta);if(d[p+"state"].as<int>()<2)d[p+"elapsed"]=std::min(604800,d[p+"elapsed"].as<int>()+(int)delta);}
 auto n=measureJson(d);if(n>=cap)return -1;serializeJson(d,out,cap);return n;
}
}
