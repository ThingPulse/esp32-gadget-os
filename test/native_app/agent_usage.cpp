#include <ArduinoJson.h>
#include "experimental/NativeHttp.h"
#include <cassert>
#include <cstring>
#include <string>
#include <fstream>
#include <iterator>
#define AGENT_USAGE_URL "https://example.test/usage/private"
#include "../../experiments/native-app/example/agent_usage.c"
struct Node {std::string text;bool visible=true;int percent=0;};static Node nodes[128];static unsigned count;
static gadget_callback timer_cb;
static native_app::DeliveryGate gate;
static native_app::HttpDelivery delivery;
static int transport;static unsigned writes=0;
static std::string preference,local_snapshot;
static int32_t read_local(char *out,uint32_t capacity){if(local_snapshot.empty()||local_snapshot.size()>=capacity)return -1;strcpy(out,local_snapshot.c_str());return (int32_t)local_snapshot.size();}
static void logger(const char*){}
static gadget_handle create(gadget_handle,uint32_t,const char*s,gadget_callback,void*){assert(count<128);auto *n=&nodes[count++];*n={s?s:"",true,0};return n;}
static int32_t box(gadget_handle,int32_t,int32_t,int32_t,int32_t){return 0;}
static int32_t flow(gadget_handle,uint32_t,uint32_t,uint32_t){return 0;}
static int32_t style(gadget_handle,uint32_t){return 0;}
static int32_t set(gadget_handle h,const char*s){assert(strlen(s)<=160);static_cast<Node*>(h)->text=s;return 0;}
static int32_t value(gadget_handle h,int32_t p){assert(p>=0&&p<=100);static_cast<Node*>(h)->percent=p;return 0;}
static int32_t visible(gadget_handle h,uint32_t v){static_cast<Node*>(h)->visible=v;return 0;}
static int32_t reserve(uint32_t){return 0;}
static int32_t paint(gadget_handle,uint32_t,uint32_t,uint32_t){return 0;}
static std::string activity_snapshot;
static int32_t activity_read(char *out,uint32_t cap){if(activity_snapshot.empty()||activity_snapshot.size()>=cap)return -1;strcpy(out,activity_snapshot.c_str());return activity_snapshot.size();}
static uint32_t channel_age_ms=0;
static int32_t channel_read(const char *channel,char *out,uint32_t cap,uint32_t *age_ms){
 *age_ms=channel_age_ms;
 return !strcmp(channel,"usage")?read_local(out,cap):!strcmp(channel,"activity")?activity_read(out,cap):-1;
}
static int32_t json_string(const char *s,uint32_t n,const char *key,char *out,uint32_t cap){JsonDocument d;if(deserializeJson(d,s,n)||!d[key].is<const char*>())return -1;const char *v=d[key];if(strlen(v)>=cap)return -1;strcpy(out,v);return strlen(v);}
static int32_t selected(gadget_handle,uint32_t){return 0;}
static int32_t setting_get(const char*k,char *v,uint32_t n){if(strcmp(k,"used_view")||preference.empty()||n<2)return -1;strcpy(v,preference.c_str());return 1;}
static int32_t setting_set(const char*,const char*v){++writes;preference=v;return 0;}
static const gadget_ui_api ui_api={sizeof(ui_api),create,box,flow,style,set,value,visible,selected,style,nullptr,nullptr,reserve,nullptr,nullptr,paint};
static const std::string &text(gadget_handle h){return static_cast<Node*>(h)->text;}
static gadget_handle timer(uint32_t,gadget_callback cb,void*){timer_cb=cb;return nodes;}
static void close_host(){gate.close();delivery.clear();}
static int32_t get(const char*,gadget_http_callback cb,void*u){if(!transport)delivery={1,cb,u};return transport;}
static void complete(int code,const char*s){delivery.deliver(1,gate,code,s,strlen(s));}
static const char *good="{\"schema\":1,\"codex_enabled\":1,\"claude_enabled\":0,\"codex_age\":0,\"claude_age\":-1,\"codex_primary_used\":28,\"codex_primary_minutes\":300,\"codex_primary_reset\":10,\"codex_secondary_used\":-1}";
int main(int argc,char **argv){
 for(int i=1;i<argc;++i){std::ifstream f(argv[i],std::ios::binary);std::string bytes{std::istreambuf_iterator<char>(f),{}};std::string error;
 assert(!bytes.empty());assert(native_app::validate_elf((const uint8_t*)bytes.data(),bytes.size(),error));}
 gadget_host_api api={sizeof(api),1,3,logger,nullptr,nullptr,nullptr,nullptr,timer,close_host,get,native_app::json_number_scaled,&ui_api,setting_get,setting_set,read_local,nullptr,nullptr,json_string,nullptr,activity_read,channel_read};
 gadget_app_lifecycle app={sizeof(app),1,1,nullptr};char *args[]={(char*)&api,(char*)&app};
 preference="1"; // The obsolete Used preference must not change the overview.
 for(int cycle=0;cycle<100;++cycle){count=0;gate={};transport=0;api.minor=1;assert(gadget_app_entry(2,args)==-1);api.minor=2;assert(gadget_app_entry(2,args)==-1);api.minor=3;assert(gadget_app_entry(2,args)==-1);api.minor=6;assert(gadget_app_entry(2,args)==-1);api.minor=7;assert(gadget_app_entry(2,args)==0);complete(200,good);
  assert(text(cards[0].value)=="72%");assert(!static_cast<Node*>(cards[1].box)->visible);
  open_detail(nullptr);assert(detail_view);assert(text(stat_values[0])=="--");assert(static_cast<Node*>(detail_root)->visible);close_app(nullptr);assert(!detail_view&&!gate.closing);
  unsigned before=writes;open_detail(nullptr);choose_period((void*)0);assert(period_view==0);choose_period((void*)2);assert(period_view==2);assert(writes==before);assert(text(cards[0].value)=="72%");close_app(nullptr);
  for(int i=0;i<10;++i)timer_cb(nullptr);
  assert(text(cards[0].value)=="--");assert(text(cards[0].primary).find("awaiting update")!=std::string::npos);
  refresh(nullptr);complete(200,"{}");assert(text(status).find("Refresh failed")!=std::string::npos);
  for(int i=0;i<5;++i)timer_cb(nullptr);refresh(nullptr);complete(429,"");assert(cooldown==60);
  refresh(nullptr);assert(!delivery.callback);
  for(int i=0;i<60;++i)timer_cb(nullptr);
  close_app(nullptr);assert(gate.closing);app.shutdown();assert(!delivery.deliver(1,gate,200,good,strlen(good)));
 }
 count=0;gate={};transport=0;assert(gadget_app_entry(2,args)==0);
 delivery.clear();pending=0;cooldown=0;endpoint[0]=0;
 std::string partial=std::string(good);partial.pop_back();partial+=R"(,"codex_detail_age":1,"cx_p2_v":1230,"cx_p2_u":1,"cx_p2_n":4,"cx_p2_e":6,"cx_m0_v":1230,"cx_m0_u":1,"cx_m0_bar":100})";
 received(200,partial.c_str(),partial.size(),nullptr);open_detail(nullptr);choose_period((void*)2);assert(text(chart_title)=="Known: 123K tokens (4/6 days)");choose_period(nullptr);assert(text(chart_title)=="Not reported yet (UTC)");close_app(nullptr);
 local_snapshot=good;refresh(nullptr);assert(text(cards[0].value)=="72%");assert(!pending);
 channel_age_ms=11000;cooldown=0;refresh(nullptr);assert(text(cards[0].value)=="--");assert(ages[0]==11);
 channel_age_ms=0;cooldown=0;refresh(nullptr);assert(text(cards[0].value)=="72%");
 local_snapshot.clear();for(int i=0;i<6;++i)timer_cb(nullptr);
 assert(text(cards[0].value)=="72%"); // no push retains prior sample and elapsed time
 for(int i=0;i<4;++i)timer_cb(nullptr);
 assert(text(cards[0].value)=="--"); // disappearing companion cannot renew expired allowance
 close_app(nullptr);app.shutdown();
 count=0;gate={};assert(gadget_app_entry(2,args)==0);delivery.clear();pending=0;
 activity_snapshot=R"({"schema":1,"age":0,"enabled":1,"count":1,"total":1,"attention":1,"s0_id":"a","s0_project":"gadget-os","s0_event":"Approval requested","s0_state":1,"s0_provider":0,"s0_age":0,"s0_elapsed":60})";
 refresh_activity();render();assert(activity_view);assert(text(nav[0])=="Activity (1)");select_session(nullptr);assert(activity_detail);assert(text(session_help).find("Open Codex")!=std::string::npos);
 assert(text(session_tokens)=="Tokens: Not reported");
 channel_age_ms=16000;refresh_activity();render();assert(activity_age==16);assert(sessions[0].age==16);assert(sessions[0].seconds==76);assert(session_state_value(&sessions[0])==3);
 channel_age_ms=0;refresh_activity();
 const std::string original=activity_snapshot;
 // App validation rejects bad array bounds/strings now that firmware does not filter fields.
 activity_snapshot=R"({"schema":1,"age":0,"enabled":1,"count":7,"total":7,"attention":0})";refresh_activity();assert(activity_count==1);
 activity_snapshot=R"({"schema":1,"age":0,"enabled":1,"count":1,"total":1,"attention":0,"s0_id":"a","s0_project":"bad\nname","s0_event":"Working","s0_state":0,"s0_provider":0,"s0_age":0,"s0_elapsed":1})";refresh_activity();assert(std::string(sessions[0].project)=="gadget-os");
 activity_snapshot=original;refresh_activity();
 sessions[0].tokens_v=1234;sessions[0].tokens_u=1;render();assert(text(session_tokens).find("reported tokens")!=std::string::npos);
 sessions[0].state=2;sessions[0].age=604800;assert(session_state_value(&sessions[0])==2);sessions[0].state=1;sessions[0].age=0;
 close_app(nullptr);assert(!activity_detail&&!gate.closing);switch_view((void*)1);assert(!activity_view);refresh_activity();assert(!activity_view);
 activity_snapshot.clear();for(int i=0;i<16;++i)timer_cb(nullptr);assert(session_state_value(&sessions[0])==3);assert(text(nav[0])=="Activity");
 switch_view(nullptr);activity_count=6;for(unsigned i=1;i<6;++i){sessions[i]=sessions[0];sessions[i].id[0]=(char)('a'+i);sessions[i].id[1]=0;sessions[i].project[0]=(char)('A'+i);sessions[i].project[1]=0;}next_page(nullptr);assert(activity_page==1);select_session((void*)2);assert(text(session_provider)=="F");AgentSession swap=sessions[0];sessions[0]=sessions[5];sessions[5]=swap;render();assert(text(session_provider)=="F");
 activity_count=0;render();assert(text(session_state)=="Session no longer listed");close_app(nullptr);close_app(nullptr);app.shutdown();
 puts("PASS: Agent Usage remaining and usage periods, disabled provider, expired data, invalid response, rate limit and 100 close cycles");
}
