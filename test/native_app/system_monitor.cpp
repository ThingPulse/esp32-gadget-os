#include "experimental/NativeHttp.h"
#include <ArduinoJson.h>
#include <cassert>
#include <cstring>
#include <string>
#include <fstream>
#include <iterator>
#include "../../experiments/native-app/example/system_monitor.c"
struct Node {std::string text;bool shown=true;int percent=0;};static Node nodes[128];static unsigned count;static bool closed;static int reads=0,refreshes=0;static gadget_callback timer_cb;static std::string snapshot;
static gadget_handle create(gadget_handle,uint32_t,const char*t,gadget_callback,void*){assert(count<128);auto *p=&nodes[count++];*p={t?t:"",true,0};return p;}
static int32_t box(gadget_handle,int32_t,int32_t,int32_t,int32_t){return 0;}static int32_t flow(gadget_handle,uint32_t,uint32_t,uint32_t){return 0;}static int32_t style(gadget_handle,uint32_t){return 0;}
static int32_t text(gadget_handle h,const char*t){assert(strlen(t)<=160);((Node*)h)->text=t;return 0;}static int32_t value(gadget_handle h,int32_t v){assert(v>=0&&v<=100);((Node*)h)->percent=v;return 0;}static int32_t visible(gadget_handle h,uint32_t v){((Node*)h)->shown=v;return 0;}
static int32_t chart(gadget_handle,const int32_t*v,uint32_t n){assert(n<=60);for(unsigned i=0;i<n;++i)assert(v[i]>=-1&&v[i]<=100);return 0;}
static gadget_handle timer(uint32_t,gadget_callback cb,void*){timer_cb=cb;return nodes;}static void close_host(){closed=true;}
static int32_t read_system(char*out,uint32_t cap){++reads;if(!paired)return -2;if(snapshot.empty())return -1;assert(snapshot.size()<cap);strcpy(out,snapshot.c_str());return snapshot.size();}
static uint32_t channel_age_ms=0;
static int32_t channel_read(const char *channel,char *out,uint32_t cap,uint32_t *age_ms){
 assert(!strcmp(channel,"system"));*age_ms=channel_age_ms;return read_system(out,cap);
}
static int32_t refresh_system(){++refreshes;return 0;}
static int32_t json_string(const char*s,uint32_t n,const char*k,char*out,uint32_t cap){JsonDocument d;if(deserializeJson(d,s,n)||!d[k].is<const char*>())return -1;const char*v=d[k];size_t len=strlen(v);if(len>=cap)return -1;memcpy(out,v,len+1);return len;}
static int32_t reserve(uint32_t n){assert(n<=128);return 0;}
static const gadget_ui_api ui_api={sizeof(ui_api),create,box,flow,style,text,value,visible,style,style,chart,style,reserve};
static std::string shown(gadget_handle h){return ((Node*)h)->text;}
int main(int argc,char **argv){
 for(int i=1;i<argc;++i){std::ifstream f(argv[i],std::ios::binary);std::string b{std::istreambuf_iterator<char>(f),{}};std::string error;assert(native_app::validate_elf((const uint8_t*)b.data(),b.size(),error));}
 gadget_host_api api={sizeof(api),1,4,nullptr,nullptr,nullptr,nullptr,nullptr,timer,close_host,nullptr,native_app::json_number_scaled,&ui_api,nullptr,nullptr,nullptr,read_system,refresh_system,json_string,nullptr,nullptr,channel_read};
 gadget_app_lifecycle lifecycle={sizeof(lifecycle),1,4,nullptr};char*args[]={(char*)&api,(char*)&lifecycle};
 for(int cycle=0;cycle<100;++cycle){snapshot.clear();count=0;closed=false;api.minor=3;assert(gadget_app_entry(2,args)==-1);api.minor=6;assert(gadget_app_entry(2,args)==-1);api.minor=11;assert(gadget_app_entry(2,args)==0);assert(count<=128);assert(shown(status)=="Waiting for computer");
 snapshot=R"({"schema":1,"age":0,"cpu":38,"ram":68,"cores":8,"disk_total_mib":1000,"disk_free_mib":1000,"ct0_name":"Worker","ct0_value":125,"mt0_name":"Large","mt0_value":1024,"cpu_hist":"ff0064","ram_hist":"3244","battery":0,"power":0})";timer_cb(nullptr);assert(shown(metric[0])=="38%");assert(shown(connection)=="Live");assert(shown(power_text)=="Battery 0%");assert(((Node*)disk_bar)->percent==0);choose((void*)1);assert(((Node*)panels[1])->shown);assert(shown(proc_names[0])=="Worker");sorting((void*)1);assert(shown(proc_names[0])=="Large");assert(shown(proc_values[0])=="1 GB");choose((void*)2);assert(((Node*)panels[2])->shown);
 snapshot=R"({"schema":1,"age":0,"cores":10,"cpu_cores":"001432ff6465323c4650"})";timer_cb(nullptr);choose((void*)3);
 assert(((Node*)panels[3])->shown&&shown(core_values[0])=="0%"&&shown(core_values[3])=="--"&&shown(core_values[4])=="100%"&&shown(core_values[5])=="--");
 core_paging((void*)1);assert(shown(core_names[0])=="Core 9"&&shown(core_values[1])=="80%");close_app(nullptr);assert(page==0&&!closed);
 snapshot=R"({"schema":1,"age":0,"cores":8})";timer_cb(nullptr);assert(core_count==0&&shown(core_title)=="Per-core data unavailable");
 snapshot=R"({"schema":1,"age":0,"ram":84,"ram10":837,"ram_hist":"545453ff","ram_frac":"247f"})";timer_cb(nullptr);
 assert(shown(metric[1])=="83.7%");assert(shown(memory_range)=="82-86% / 2 min");
 assert(chart_data[1][0]==55&&chart_data[1][1]==60&&chart_data[1][2]==42&&chart_data[1][3]==-1);
 snapshot=R"({"schema":1,"age":0,"ram":84,"ram_hist":"5454"})";timer_cb(nullptr);
 assert(shown(metric[1])=="84%");assert(shown(memory_range)=="83-85% / 2 min");assert(chart_data[1][0]==50);
 snapshot=R"({"schema":1,"age":0,"ram10":1000,"ram_hist":"0064","ram_frac":"00"})";timer_cb(nullptr);
 assert(shown(metric[1])=="100.0%");assert(chart_data[1][0]==0&&chart_data[1][1]==100);
 snapshot=R"({"schema":1,"age":0,"ram":84,"ram10":1001,"ram_hist":"6464","ram_frac":"9f"})";timer_cb(nullptr);
 assert(shown(metric[1])=="84%");assert(chart_data[1][0]==-1&&chart_data[1][1]==-1);
 snapshot=R"({"schema":1,"age":11,"disk_total_mib":1000,"disk_free_mib":0})";timer_cb(nullptr);assert(shown(connection)=="Offline");assert(((Node*)disk_bar)->percent==100);assert(shown(metric[0])=="--");
 snapshot=R"({"schema":1,"age":0,"cpu":0})";timer_cb(nullptr);assert(shown(connection)=="Live");assert(shown(metric[0])=="0%");unsigned before=refreshes;refresh(nullptr);refresh(nullptr);assert(refreshes==before+1);
 channel_age_ms=12000;timer_cb(nullptr);assert(shown(connection)=="Offline");assert(age==12);channel_age_ms=0;
 snapshot=R"({"schema":1,"age":0,"cpu":101,"ram":2147483647,"battery":101,"power":9,"ct0_name":"bad\nname","ct0_value":1001,"future_field":{"anything":true}})";timer_cb(nullptr);
 assert(shown(metric[0])=="--"&&shown(metric[1])=="--");assert(battery==-1&&power==-1&&names[0][0][0]==0&&proc_val[0][0]==-1);
 snapshot.clear();paired=0;timer_cb(nullptr);assert(shown(status)=="Pair in Add apps > Pair computer");close_app(nullptr);assert(closed);lifecycle.shutdown();}
 puts("PASS: System Monitor 100 lifecycles, ABI gate, views/sorting, refresh throttling, stale/reconnect, missing/zero and capacity bounds");
}
