/* Desktop-only adapter: runs the exact installable app source against LVGL.
 * It does not pretend to execute an Xtensa ELF on the desktop. */
#ifndef ARDUINO
#include "AppRegistry.h"
#include "experimental/NativeUi.h"
#include "experimental/NativeSettings.h"
#include "experimental/CompanionData.h"
#include "ui/I18n.h"
#include "ui/UiNav.h"
#include <ArduinoJson.h>
#include <fstream>
#include <iterator>
#include <cstdlib>
#include <cmath>
#include <climits>
#include <cstring>
#include <ctime>
#include "../../experiments/native-app/sdk/gadget_app.h"
#define AGENT_USAGE_URL "https://preview.invalid/usage"
#define gadget_app_entry agent_usage_preview_entry
namespace usage_source {
#include "../../experiments/native-app/example/agent_usage.c"
}
#undef gadget_app_entry
#undef AGENT_USAGE_URL
namespace {
lv_obj_t *screen_obj, *home;
lv_timer_t *app_timer;
gadget_app_lifecycle lifecycle;
bool closing=false;
const char *sample="{\"schema\":1,\"codex_enabled\":1,\"claude_enabled\":1,\"codex_age\":0,\"claude_age\":0,\"codex_primary_used\":28,\"codex_primary_minutes\":300,\"codex_primary_reset\":7200,\"codex_secondary_used\":46,\"codex_secondary_minutes\":10080,\"codex_secondary_reset\":259200,\"claude_primary_used\":61,\"claude_primary_minutes\":300,\"claude_primary_reset\":3600,\"claude_secondary_used\":23,\"claude_secondary_minutes\":10080,\"claude_secondary_reset\":432000}";
void log_message(const char *s){printf("[Agent Usage preview] %s\n",s);}
gadget_handle timer_create(uint32_t ms,gadget_callback cb,void *user){
 struct TimerContext {gadget_callback callback;void *user;};static TimerContext context;context={cb,user};app_timer=lv_timer_create([](lv_timer_t *t){auto *c=static_cast<TimerContext*>(lv_timer_get_user_data(t));if(!closing)c->callback(c->user);},ms,&context);return app_timer;
}
void close_app(){if(closing)return;closing=true;native_app::ui::suspend();lv_async_call([](void*){if(app_timer)lv_timer_delete(app_timer);app_timer=nullptr;native_app::ui::end();native_app::settings::end();if(lifecycle.shutdown)lifecycle.shutdown();ui_load_screen(home,true);},nullptr);}
void dispatch(gadget_callback cb,void *user){if(!closing&&cb)cb(user);}
int32_t setting_get(const char *k,char *v,uint32_t n){return closing?-1:native_app::settings::get(k,v,n);}
int32_t setting_set(const char *k,const char *v){return closing?-1:native_app::settings::set(k,v);}
int32_t get_json(const char*,gadget_http_callback cb,void *user){
 std::string body=sample;
 const char *path=getenv("GADGET_USAGE_SNAPSHOT");
 if(path){std::ifstream f(path);body.clear();char c;while(body.size()<=2048&&f.get(c))body+=c;}
 else{
  JsonDocument d;deserializeJson(d,body);d["sample"]=1;d["codex_detail_age"]=0;
  d["codex_lifetime_v"]=124;d["codex_lifetime_u"]=2;d["codex_peak_v"]=850;d["codex_peak_u"]=1;
  d["codex_streak"]=8;d["codex_best_streak"]=14;d["codex_longest_sec"]=540;
  for(int i=0;i<7;++i){std::string key="codex_d"+std::to_string(i);d[key+"_v"]=120+i*90;d[key+"_u"]=1;d[key+"_date"]=1101+i;d[key+"_bar"]=20+i*12;}
  for(int i=0;i<3;++i){std::string k="cx_p"+std::to_string(i);d[k+"_v"]=i==0?660:i==1?2730:7500;d[k+"_u"]=1;d[k+"_n"]=i==0?1:i==1?7:30;d[k+"_e"]=i==0?1:i==1?7:30;}
  for(int i=0;i<6;++i){std::string k="cx_m"+std::to_string(i);d[k+"_v"]=1000+i*100;d[k+"_u"]=1;d[k+"_bar"]=50+i*10;}
  body.clear();serializeJson(d,body);
 }
 if(body.size()>2048){cb(GADGET_HTTP_TOO_LARGE,nullptr,0,user);return 0;}
 // Live CLI snapshots retain wall-clock observation age even after restart or
 // when the companion stops. Legacy fixture files have no generated_at field.
 if(path&&!body.empty()){
  JsonDocument d;if(!deserializeJson(d,body)&&d["generated_at"].is<int64_t>()){
   const int64_t delta=std::max<int64_t>(0,std::time(nullptr)-d["generated_at"].as<int64_t>());
   for(const char *provider:{"codex","claude"}){
    const std::string p=provider;int age=d[p+"_age"];
    if(age>=0)d[p+"_age"]=int(std::min<int64_t>(604800,age+delta));
    for(const char *window:{"primary","secondary"}){
     const std::string key=p+"_"+window;
     if(d[key+"_used"].as<double>()>=0){int64_t left=d[key+"_reset"].as<int64_t>()-delta;if(left<=0)d[key+"_used"]=-1;else d[key+"_reset"]=left;}
    }
   }
   if(d["codex_detail_age"].is<int>()&&d["codex_detail_age"].as<int>()>=0)d["codex_detail_age"]=int(std::min<int64_t>(604800,d["codex_detail_age"].as<int>()+delta));
   body.clear();serializeJson(d,body);
  }
 }

 cb(body.empty()?503:200,body.c_str(),static_cast<uint32_t>(body.size()),user);return 0;
}
int32_t json_number(const char *s,uint32_t n,const char *key,int32_t scale,int32_t *out){
 if(!s || !n || n>2048 || !out || scale<1)return -1;JsonDocument doc;
 if(deserializeJson(doc,s,n) || !doc.is<JsonObject>() || !doc[key].is<double>())return -1;
 double v=std::round(doc[key].as<double>()*scale);if(!std::isfinite(v)||v<INT32_MIN||v>INT32_MAX)return -1;*out=(int32_t)v;return 0;
}
int32_t channel_read(const char *channel,char *out,uint32_t cap,uint32_t *age_ms){
 if(!channel||strcmp(channel,"activity")||!age_ms)return -1;*age_ms=0;
 const char *path=getenv("GADGET_ACTIVITY_SNAPSHOT");std::string body;
 if(path){std::ifstream f(path);char c;while(body.size()<=2048&&f.get(c))body+=c;if(body.empty()||body.size()>2048)return -1;
  JsonDocument d;if(deserializeJson(d,body)||!d["generated_at"].is<int64_t>())return -1;
  auto delta=std::max<int64_t>(0,std::time(nullptr)-d["generated_at"].as<int64_t>());
  if(delta>=604800)return -1;*age_ms=static_cast<uint32_t>(delta*1000);
 }else body=R"({"schema":1,"age":0,"enabled":1,"count":3,"total":3,"attention":1,"s0_id":"sample-codex","s0_project":"gadget-os","s0_event":"Approval requested","s0_state":1,"s0_provider":0,"s0_elapsed":252,"s0_age":0,"s1_id":"sample-claude","s1_project":"website","s1_event":"Using Bash","s1_state":0,"s1_provider":1,"s1_elapsed":98,"s1_age":0,"s2_id":"sample-docs","s2_project":"docs","s2_event":"Ready to review","s2_state":2,"s2_provider":0,"s2_elapsed":168,"s2_age":120})";
 if(!path){JsonDocument d;deserializeJson(d,body);d["sample"]=1;body.clear();serializeJson(d,body);}
 if(body.size()>=cap)return -1;memcpy(out,body.c_str(),body.size()+1);return body.size();
}
int32_t json_string(const char *s,uint32_t n,const char *key,char *out,uint32_t cap){JsonDocument d;if(deserializeJson(d,s,n)||!d[key].is<const char*>())return -1;const char *v=d[key];if(strlen(v)>=cap)return -1;strcpy(out,v);return strlen(v);}
void launch(){
 home=lv_screen_active();screen_obj=lv_obj_create(nullptr);closing=false;
 lv_obj_set_style_bg_color(screen_obj,lv_color_hex(0x172131),0);
 lv_obj_set_style_text_color(screen_obj,lv_color_white(),0);
 lv_obj_set_style_text_font(screen_obj,LV_FONT_DEFAULT,0);
 lv_obj_set_flex_flow(screen_obj,LV_FLEX_FLOW_COLUMN);
 lv_obj_set_flex_align(screen_obj,LV_FLEX_ALIGN_CENTER,LV_FLEX_ALIGN_CENTER,LV_FLEX_ALIGN_CENTER);
 native_app::ui::begin(screen_obj,dispatch);native_app::settings::begin("agent-usage-preview");
 static const gadget_host_api api={sizeof(api),1,7,log_message,nullptr,nullptr,nullptr,nullptr,timer_create,close_app,get_json,json_number,&native_app::ui::api,setting_get,setting_set,[](char*,uint32_t)->int32_t{return -1;},nullptr,nullptr,json_string,nullptr,nullptr,channel_read};
 lifecycle={sizeof(lifecycle),1,2,nullptr};char *args[]={(char*)&api,(char*)&lifecycle};
 ui_load_screen(screen_obj,false);usage_source::agent_usage_preview_entry(2,args);

}
#if !GADGET_OS_APP_STORE_SIMULATOR
APP_REGISTER_COLOR_RANK("Agent Dashboard\nPreview",LV_SYMBOL_CHARGE,launch,0x237e77,90);
#endif
}
void agent_usage_simulator_launch(){launch();}
#endif
