#include "experimental/CompanionChannels.h"
#include "experimental/CompanionData.h"
#include "experimental/CompanionRequest.h"
#include <ArduinoJson.h>
#include <cassert>
#include <cstring>
#include <iostream>
using native_app::companion::UsageSnapshot;
int main(){
    using namespace native_app::companion;
    Request request;
    const std::string host="192.168.1.4:8082";
    const std::string prefix="POST /v1/agent-usage HTTP/1.1\r\nHost: "+host+"\r\nContent-Length: 2\r\n";
    const auto wire=prefix+"X-Companion-Key: abc\r\n\r\n{}";
    for(size_t n=0;n<wire.find("\r\n\r\n")+4;++n)assert(parse_request(wire.substr(0,n),host,request)==0);
    assert(parse_request(wire,host,request)==1);assert(request.length==2&&request.key=="abc"&&request.route=="/v1/agent-usage");
    for(auto extra:{"Content-Length: 2\r\n","Host: 192.168.1.4:8082\r\n","Transfer-Encoding: chunked\r\n","Origin: http://evil.invalid\r\n","Origin:\r\n","Bad Header: value\r\n","X-Companion-Key: a\r\nX-Companion-Key: b\r\n"})
        assert(parse_request(prefix+extra+"\r\n{}",host,request)<0);
    assert(parse_request(wire,"192.168.1.5:8082",request)<0);
    assert(parse_request(std::string(2049,'a'),host,request)==-431);
    assert(parse_request("POST / HTTP/1.1\r\nHost: "+host+"\r\nContent-Length: 9999\r\n\r\n",host,request)<0);

    UsageSnapshot s;char out[2049];assert(s.read(out,sizeof out,0)==-1);
    const char *good=R"({"schema":1,"codex_enabled":1,"claude_enabled":0,"codex_age":30,"claude_age":-1,"codex_primary_used":28.4,"codex_primary_minutes":300,"codex_primary_reset":60,"codex_secondary_used":-1,"cx_p2_v":123,"cx_p2_u":2,"cx_p2_n":4,"cx_p2_e":6,"cx_m0_v":99,"cx_m0_u":1,"cx_m0_bar":50,"codex_detail_age":20,"codex_lifetime_v":123,"codex_lifetime_u":3,"codex_streak":8,"codex_best_streak":999999,"codex_d0_v":0,"codex_d0_u":0,"codex_d0_date":1101,"codex_d0_bar":0,"secret":"must not survive"})";
    assert(s.update(good,strlen(good),4294967200ULL));
    int n=s.read(out,sizeof out,4294977200ULL);assert(n>0);JsonDocument d;assert(!deserializeJson(d,out));
    assert(d["codex_age"]==40);assert(d["codex_primary_reset"]==50);assert(d["cx_p2_v"]==123);assert(d["cx_p2_n"]==4);assert(d["cx_m0_bar"]==50);assert(d["codex_detail_age"]==30);assert(d["codex_lifetime_v"]==123);assert(d["codex_lifetime_u"]==3);assert(d["codex_d0_v"]==0);assert(d["codex_best_streak"].isNull());assert(!d["secret"].is<const char*>());
    assert(s.read(out,2,4294977200ULL)==-1);
    assert(s.read(out,sizeof out,4294977200ULL)>0);deserializeJson(d,out);assert(d["codex_age"]==40);
    assert(!s.update("{}",2,0));assert(!s.update(good,2049,0));
    for(auto key:{"codex_enabled","codex_age","codex_primary_used","codex_primary_minutes","codex_primary_reset"}){
        JsonDocument bad;deserializeJson(bad,good);bad[key]="bad";std::string b;serializeJson(bad,b);assert(!s.update(b.data(),b.size(),0));
    }
    assert(s.read(out,sizeof out,4295027200ULL)>0);deserializeJson(d,out);assert(d["codex_primary_used"]==-1);
    assert(d["codex_age"]==90); // expiry never becomes 100% remaining
    assert(s.read(out,sizeof out,4294967200ULL+604800000ULL)==-1);
    assert(s.read(out,sizeof out,0)==-1);
    s.clear();assert(s.read(out,sizeof out,4294977200ULL)==-1);
    SystemSnapshot sys;const char *sample=R"({"schema":1,"age":0,"host":"Mac","cpu":38,"ram":68,"cpu_hist":"ff003264","ct0_name":"node","ct0_pid":123,"ct0_value":90,"secret":"never forward","up_bps":-123,"power":99})";
    assert(sys.update(sample,strlen(sample),1000));assert(sys.read(out,sizeof out,12000)>0);deserializeJson(d,out);assert(d["age"]==11);assert(d["cpu"]==38);assert(d["ct0_name"]=="node");assert(d["up_bps"]==-1);assert(d["power"]==-1);assert(d["secret"].isNull());assert(sys.read(out,2,12000)==-1);assert(!sys.update("{}",2,12000));sys.clear();assert(sys.read(out,sizeof out,12000)==-1);
    ActivitySnapshot activity;
    const char *live=R"({"schema":1,"age":0,"enabled":1,"count":1,"total":1,"attention":1,"s0_id":"abc","s0_project":"gadget-os","s0_event":"Approval requested","s0_state":1,"s0_provider":0,"s0_elapsed":20,"s0_age":2,"prompt":"secret"})";
    assert(activity.update(live,strlen(live),1000));assert(activity.read(out,sizeof out,17000)>0);deserializeJson(d,out);assert(d["age"]==16);assert(d["s0_age"]==18);assert(d["s0_elapsed"]==36);assert(d["prompt"].isNull());
    assert(activity.read(out,10,17000)==-1);assert(activity.read(out,sizeof out,0)==-1);
    JsonDocument tokens;deserializeJson(tokens,live);tokens["s0_tokens_v"]=1234;tokens["s0_tokens_u"]=1;
    std::string tokenJson;serializeJson(tokens,tokenJson);assert(activity.update(tokenJson.c_str(),tokenJson.size(),1000));
    assert(activity.read(out,sizeof out,17000)>0);deserializeJson(d,out);assert(d["s0_tokens_v"]==1234);assert(d["s0_tokens_u"]==1);
    for(auto key:{"s0_tokens_v","s0_tokens_u"}){auto saved=tokens[key].as<int>();tokens[key]=-1;tokenJson.clear();serializeJson(tokens,tokenJson);assert(!activity.update(tokenJson.c_str(),tokenJson.size(),0));tokens[key]=saved;}
    tokens.remove("s0_tokens_u");tokenJson.clear();serializeJson(tokens,tokenJson);assert(!activity.update(tokenJson.c_str(),tokenJson.size(),0));
    for(auto key:{"s0_state","s0_provider","s0_elapsed","s0_age","count","attention"}){JsonDocument bad;deserializeJson(bad,live);bad[key]=-1;std::string json;serializeJson(bad,json);assert(!activity.update(json.c_str(),json.size(),0));}
    assert(!activity.update("{}",2,0));assert(activity.read(out,sizeof out,17000)>0);activity.clear();assert(activity.read(out,sizeof out,17000)==-1);
    ChannelStore channels;uint32_t age_ms=999;
    std::string app,channel;
    assert(channel_route("/v1/apps/future-app/metrics",app,channel)&&app=="future-app"&&channel=="metrics");
    for(const char *route:{"/v1/apps/../metrics","/v1/apps/a/b/c","/v1/apps/a/","/v1/apps/A/b","/v1/apps/a/b?x=1","/v1/apps/a/%2f"})assert(!channel_route(route,app,channel));
    // Future schemas/fields survive byte-for-byte, including app-owned ages.
    const char *future=" {\"schema\":42,\"age\":7,\"new_metric\":{\"values\":[1,2,3]},\"unicode\":\"é\"} \n";
    assert(channels.publish("future-app","metrics",future,strlen(future),1000)==200);
    assert(channels.read("future-app","metrics",out,sizeof out,&age_ms,9876)==(int)strlen(future));
    assert(std::string(out)==future&&age_ms==8876);
    assert(channels.read("other-app","metrics",out,sizeof out,&age_ms,9876)==-1);
    assert(channels.read("future-app","other",out,sizeof out,&age_ms,9876)==-1);
    assert(channels.read("future-app","metrics",out,strlen(future),&age_ms,9876)==-1);
    assert(channels.read("future-app","metrics",out,sizeof out,nullptr,9876)==-1);
    assert(channels.read("future-app","metrics",out,sizeof out,&age_ms,999)==-1);
    assert(channels.publish("future-app","metrics","{}",2,1499)==429);
    for(const char *invalid:{"[]","null","{","{} {}","{} garbage}","{\"x\":NaN}","{\"x\":1} trailing"})assert(channels.publish("future-app","metrics",invalid,strlen(invalid),2000)==400);
    const char embedded[]={ '{','}',0,' ' };assert(channels.publish("future-app","metrics",embedded,sizeof embedded,2000)==400);
    std::string huge(2049,' ');assert(channels.publish("future-app","metrics",huge.data(),huge.size(),2000)==400);
    std::string deep="{}";for(int i=0;i<9;++i)deep="{\"x\":"+deep+"}";assert(channels.publish("future-app","metrics",deep.data(),deep.size(),2000)==400);
    assert(channels.read("future-app","metrics",out,sizeof out,&age_ms,2000)>0&&std::string(out)==future);
    assert(channels.read("future-app","metrics",out,sizeof out,&age_ms,1000+channel_max_age_ms)==-1);
    channels.clear();assert(channels.read("future-app","metrics",out,sizeof out,&age_ms,2000)==-1);
    for(size_t i=0;i<channel_slot_limit+1;++i)assert(channels.publish("test-app","channel-"+std::to_string(i),"{}",2,1000+i*500)==200);
    assert(channels.read("test-app","channel-0",out,sizeof out,&age_ms,10000)==-1); // bounded oldest-entry eviction
    assert(channels.read("test-app","channel-1",out,sizeof out,&age_ms,10000)==2);
    channels.clear();
    const uint64_t rollover=uint64_t(1)<<32;
    assert(channels.publish("test-app","clock","{}",2,rollover-100)==200);
    assert(channels.read("test-app","clock",out,sizeof out,&age_ms,rollover+900)==2&&age_ms==1000);
    puts("PASS: companion legacy adapters and opaque channels: isolation, bounds, exact payload, expiry, eviction, rollover and revocation");
}
