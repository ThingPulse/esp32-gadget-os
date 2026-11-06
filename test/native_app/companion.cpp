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
    puts("PASS: companion input bounds, sanitized snapshots, stale age, expiry, rollover and revocation");
}
