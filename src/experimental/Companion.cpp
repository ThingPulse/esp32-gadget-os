#if GADGET_OS_NATIVE_APP_LOADER
#include "experimental/Companion.h"
#include "experimental/CompanionData.h"
#include "experimental/CompanionRequest.h"
#include "experimental/NativeApp.h"
#include "AppRegistry.h"
#include "ui/I18n.h"
#include <Arduino.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <mbedtls/sha256.h>
#include <lwip/sockets.h>
#include <cerrno>
#include <esp_timer.h>
#include <algorithm>
#include <cctype>

namespace native_app::companion {
namespace {
WiFiServer server(8082);WiFiUDP discovery;WiFiClient client;
UsageSnapshot usage;SystemSnapshot system_data;uint32_t system_refresh_id=0,last_system_request=0;
bool initialized=false,listening=false,requested=false,visible=false,closing=false,connected=false,approved=false;
std::string paired_token,paired_name,pending_token,pending_name,headers,output,body;
size_t sent=0,content_length=0;
bool parsed=false;std::string method,route,supplied;
uint32_t deadline=0,pair_deadline=0,last_discovery=0,last_request=0;
lv_obj_t *message=nullptr,*accept_button=nullptr;
std::string id(){auto s=std::string(WiFi.macAddress().c_str());s.erase(std::remove(s.begin(),s.end(),':'),s.end());return s;}
bool equal(const std::string&a,const std::string&b){if(a.size()!=64||b.size()!=64)return false;unsigned d=0;for(size_t i=0;i<64;++i)d|=a[i]^b[i];return d==0;}
bool token_valid(const std::string&s){return s.size()==64&&s.find_first_not_of("0123456789abcdef")==std::string::npos;}
void clear_pending(){pending_token.clear();pending_name.clear();approved=false;}
void drop(){client.stop();connected=false;headers.clear();body.clear();output.clear();sent=content_length=0;parsed=false;method.clear();route.clear();supplied.clear();}
void reply(int status,const char *json){output="HTTP/1.1 "+std::to_string(status)+" Response\r\nContent-Type: application/json\r\nCache-Control: no-store\r\nConnection: close\r\nContent-Length: "+std::to_string(strlen(json))+"\r\n\r\n"+json;}
std::string code(const std::string &token){unsigned char digest[32];mbedtls_sha256((const unsigned char*)token.data(),token.size(),digest,0);uint32_t n=(uint32_t(digest[0])<<24)|(uint32_t(digest[1])<<16)|(uint32_t(digest[2])<<8)|digest[3];char s[7];snprintf(s,sizeof s,"%06u",unsigned(n%1000000));return s;}
bool save(const std::string &token,const std::string &name){
    Preferences prefs;if(!prefs.begin("companion",false))return false;
    JsonDocument d;d["token"]=token;d["name"]=name;std::string record;serializeJson(d,record);
    bool ok=prefs.putString("pair",record.c_str())==record.size();prefs.end();return ok;
}
void pair_message(){
    if(!message)return;
    if(approved)lv_label_set_text(message,("Connected to "+paired_name+".\nOpen an app on Installed apps.").c_str());
    else if(!pending_token.empty())lv_label_set_text(message,("Pair with "+pending_name+"?\n\nCode: "+code(pending_token)+"\n\nConfirm only if the terminal shows the same code.").c_str());
    else lv_label_set_text(message,("Run Gadget Companion on your computer.\n\nWaiting for 2 minutes…"+std::string(paired_name.empty()?"":"\n\nCurrently paired: "+paired_name)).c_str());
    if(accept_button){if(!pending_token.empty()&&!approved)lv_obj_remove_flag(accept_button,LV_OBJ_FLAG_HIDDEN);else lv_obj_add_flag(accept_button,LV_OBJ_FLAG_HIDDEN);}
}
void handle(){
    if(method=="POST"&&route=="/v1/pair"){
        if(!visible||approved||int32_t(millis()-pair_deadline)>=0){reply(409,"{\"error\":\"Open Add apps > Pair computer on the device first.\"}");return;}
        JsonDocument d;if(deserializeJson(d,body)||!d["token"].is<const char*>()||!d["name"].is<const char*>()){reply(400,"{\"error\":\"Invalid pairing request\"}");return;}
        std::string t=d["token"].as<std::string>(),name=d["name"].as<std::string>();
        if(!token_valid(t)||name.empty()||name.size()>48||std::any_of(name.begin(),name.end(),[](unsigned char c){return c<32||c>126;})){reply(400,"{\"error\":\"Invalid pairing request\"}");return;}
        if(!pending_token.empty()&&!equal(t,pending_token)){reply(409,"{\"error\":\"Another pairing is pending\"}");return;}
        pending_token=t;pending_name=name;pair_message();reply(202,"{\"pending\":true}");return;
    }
    if(method=="GET"&&route=="/v1/status"){
        if(equal(supplied,paired_token)){std::string response="{\"paired\":true,\"protocol\":1,\"system\":1,\"systemRefresh\":"+std::to_string(system_refresh_id)+"}";reply(200,response.c_str());return;}
        if(visible&&!approved&&equal(supplied,pending_token)){reply(202,"{\"pending\":true}");return;}
        reply(403,"{\"error\":\"Pairing required\"}");return;
    }
    if(!equal(supplied,paired_token)){reply(403,"{\"error\":\"Pairing required\"}");return;}
    if(method=="POST"&&route=="/v1/system"){
        if(millis()-last_system_request<500){reply(429,"{}");return;}last_system_request=millis();
        if(!system_data.update(body.data(),body.size(),esp_timer_get_time()/1000)){reply(422,"{}");return;}reply(200,"{\"ok\":true}");return;
    }
    if(method=="POST"&&route=="/v1/agent-usage"){
        if(millis()-last_request<500){reply(429,"{\"error\":\"Too frequent\"}");return;}
        last_request=millis();
        if(!usage.update(body.data(),body.size(),esp_timer_get_time()/1000)){reply(422,"{\"error\":\"Invalid usage summary\"}");return;}
        reply(200,"{\"ok\":true}");return;
    }
    reply(404,"{\"error\":\"Unknown endpoint\"}");
}
void parse(){
    Request r;int result=parse_request(headers,std::string(WiFi.localIP().toString().c_str())+":8082",r);
    if(result<0){reply(-result,"{}");return;}if(!result)return;
    method=r.method;route=r.route;supplied=r.key;content_length=r.length;
    body=headers.substr(r.body_offset);headers.clear();parsed=true;
}
void network(){
    if(!connected){client=server.accept();if(!client)return;connected=true;deadline=millis()+5000;}
    if(int32_t(millis()-deadline)>=0){drop();return;}
    if(output.empty()){
        uint8_t bytes[512];int n=client.available();if(n>0){n=client.read(bytes,std::min(n,512));if(n>0){if(parsed)body.append((char*)bytes,n);else{headers.append((char*)bytes,n);parse();}}}
        else if(!client.connected()){drop();return;}
        if(output.empty()&&parsed){if(body.size()>content_length)reply(400,"{}");else if(body.size()==content_length)handle();}
    }
    if(!output.empty()){
        int n=::send(client.fd(),output.data()+sent,std::min<size_t>(512,output.size()-sent),MSG_DONTWAIT);
        if(n>0)sent+=n;else if(n<0&&errno!=EAGAIN&&errno!=EWOULDBLOCK){drop();return;}
        if(sent==output.size())drop();
    }
}
void discover(){
    int n=discovery.parsePacket();if(!n)return;
    char buf[128];int size=discovery.read(buf,sizeof(buf)-1);discovery.flush();
    if(n>127||size<=0||millis()-last_discovery<100)return;buf[size]=0;
    const std::string request(buf,size),prefix="GADGET_COMPANION_DISCOVER_1 ";
    if(request.rfind(prefix,0)!=0)return;
    auto nonce=request.substr(prefix.size());if(nonce.size()!=32||nonce.find_first_not_of("0123456789abcdef")!=std::string::npos)return;
    last_discovery=millis();JsonDocument d;d["protocol"]=1;d["nonce"]=nonce;d["id"]=id();d["name"]="Gadget "+id().substr(6);d["port"]=8082;
    std::string response;serializeJson(d,response);discovery.beginPacket(discovery.remoteIP(),discovery.remotePort());discovery.write((const uint8_t*)response.data(),response.size());discovery.endPacket();
}
void show(){
    requested=false;visible=true;closing=false;clear_pending();pair_deadline=millis()+120000;native_launcher_release();
    auto *screen=lv_screen_active();lv_obj_set_flex_flow(screen,LV_FLEX_FLOW_COLUMN);lv_obj_set_style_pad_all(screen,14,0);
    auto *title=lv_label_create(screen);lv_label_set_text(title,"Pair computer");
    message=lv_label_create(screen);lv_obj_set_width(message,lv_pct(100));lv_obj_set_style_text_font(message,i18n::font14(),0);
    auto button=[&](const char *text,lv_event_cb_t cb){auto *b=lv_button_create(screen);lv_obj_set_size(b,lv_pct(100),48);auto *l=lv_label_create(b);lv_label_set_text(l,text);lv_obj_center(l);lv_obj_add_event_cb(b,cb,LV_EVENT_CLICKED,nullptr);return b;};
    accept_button=button("Confirm pairing",[](lv_event_t*){
        if(pending_token.empty()||int32_t(millis()-pair_deadline)>=0)return;
        if(!save(pending_token,pending_name)){lv_label_set_text(message,"Could not save pairing. Try again.");return;}
        paired_token=pending_token;paired_name=pending_name;usage.clear();system_data.clear();approved=true;pair_message();
    });
    button("Forget computer",[](lv_event_t*){if(save("","")){paired_token.clear();paired_name.clear();usage.clear();system_data.clear();clear_pending();pair_message();}});
    button("Back",[](lv_event_t*){closing=true;});pair_message();
}
}
void request_pairing(){if(!native_app::busy()&&native_launcher_is_active())requested=true;}
bool active(){return requested||visible;}
int32_t read_system(char *out,uint32_t n){return paired_token.empty()?-2:system_data.read(out,n,esp_timer_get_time()/1000);}
int32_t refresh_system(){if(paired_token.empty())return -2;++system_refresh_id;return 0;}
int32_t read_usage(char *out,uint32_t capacity){return usage.read(out,capacity,esp_timer_get_time()/1000);}
void poll(){
    if(!initialized){initialized=true;Preferences p;if(p.begin("companion",true)){JsonDocument d;if(!deserializeJson(d,p.getString("pair","{}"))){std::string t=d["token"]|"";if(token_valid(t)){paired_token=t;paired_name=d["name"]|"Computer";}}p.end();}}
    if(requested)show();
    if(visible&&!approved&&int32_t(millis()-pair_deadline)>=0){clear_pending();if(accept_button)lv_obj_add_flag(accept_button,LV_OBJ_FLAG_HIDDEN);lv_label_set_text(message,"Pairing timed out. Go back and open Pair computer again.");}
    if(closing){closing=false;visible=false;message=accept_button=nullptr;clear_pending();native_launcher_restore();}
    if(WiFi.status()!=WL_CONNECTED){if(listening){drop();server.end();discovery.stop();listening=false;}return;}
    if(!listening){server.begin();discovery.begin(8083);listening=true;}
    discover();network();
}
}
#endif
