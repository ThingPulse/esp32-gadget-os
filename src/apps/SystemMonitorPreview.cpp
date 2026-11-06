/* Desktop-only adapter: runs the exact installable app source against LVGL.
 * It does not pretend to execute an Xtensa ELF on the desktop. */
#ifndef ARDUINO
#include "AppRegistry.h"
#include "experimental/NativeUi.h"
#include "experimental/NativeSettings.h"
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
#define SYSTEM_UNUSED_URL "https://preview.invalid/usage"
#define gadget_app_entry system_monitor_preview_entry
namespace system_source {
#include "../../experiments/native-app/example/system_monitor.c"
}
#undef gadget_app_entry
#undef SYSTEM_UNUSED_URL
namespace {
lv_obj_t *screen_obj, *home;
lv_timer_t *app_timer;
gadget_app_lifecycle lifecycle;
bool closing=false;
const char *sample=R"({"schema":1,"age":2,"host":"My MacBook","cpu":38,"cores":8,"ram":68,"mem_mib":11059,"total_mib":16384,"down_bps":2400000,"up_bps":180000,"battery":82,"power":1,"uptime":273600,"volume":"Macintosh HD","disk_total_mib":512000,"disk_free_mib":188416,"cpu_hist":"20232820251e28272b302c252b2a2c302e242428282b2a262c302d2a2626","ram_hist":"3f4042434241424443424141434443424140424344454443424445444344","ct0_name":"Safari","ct0_pid":1,"ct0_value":142,"ct1_name":"node","ct1_pid":2,"ct1_value":91,"ct2_name":"WindowServer","ct2_pid":3,"ct2_value":64,"ct3_name":"Codex","ct3_pid":4,"ct3_value":48,"ct4_name":"kernel_task","ct4_pid":5,"ct4_value":21,"mt0_name":"Safari","mt0_pid":1,"mt0_value":1638,"mt1_name":"Codex","mt1_pid":4,"mt1_value":1228,"mt2_name":"node","mt2_pid":2,"mt2_value":820,"mt3_name":"WindowServer","mt3_pid":3,"mt3_value":420,"mt4_name":"kernel_task","mt4_pid":5,"mt4_value":300})";
void log_message(const char *s){printf("[System Monitor preview] %s\n",s);}
gadget_handle timer_create(uint32_t ms,gadget_callback cb,void *user){
 struct TimerContext {gadget_callback callback;void *user;};static TimerContext context;context={cb,user};app_timer=lv_timer_create([](lv_timer_t *t){auto *c=static_cast<TimerContext*>(lv_timer_get_user_data(t));if(!closing)c->callback(c->user);},ms,&context);return app_timer;
}
void close_app(){if(closing)return;closing=true;native_app::ui::suspend();lv_async_call([](void*){if(app_timer)lv_timer_delete(app_timer);app_timer=nullptr;native_app::ui::end();native_app::settings::end();if(lifecycle.shutdown)lifecycle.shutdown();ui_load_screen(home,true);},nullptr);}
void dispatch(gadget_callback cb,void *user){if(!closing&&cb)cb(user);}
int32_t setting_get(const char *k,char *v,uint32_t n){return closing?-1:native_app::settings::get(k,v,n);}
int32_t setting_set(const char *k,const char *v){return closing?-1:native_app::settings::set(k,v);}
int32_t system_read(char *out,uint32_t cap){
 std::string body=sample;const char *path=getenv("GADGET_SYSTEM_SNAPSHOT");
 if(path){std::ifstream f(path);body.clear();char c;while(body.size()<=2048&&f.get(c))body+=c;if(body.empty())return -1;}
 JsonDocument d;if(body.size()>2048||deserializeJson(d,body))return -1;
 if(d["generated_at"].is<int64_t>())d["age"]=std::min<int64_t>(2147483647,d["age"].as<int>()+std::max<int64_t>(0,std::time(nullptr)-d["generated_at"].as<int64_t>()));
 size_t n=measureJson(d);if(n>=cap)return -1;serializeJson(d,out,cap);return n;
}
int32_t json_string(const char*s,uint32_t n,const char*k,char*out,uint32_t cap){JsonDocument d;if(!s||n>2048||deserializeJson(d,s,n)||!d[k].is<const char*>())return -1;const char*v=d[k];size_t len=strlen(v);if(len>=cap)return -1;memcpy(out,v,len+1);return len;}
int32_t json_number(const char *s,uint32_t n,const char *key,int32_t scale,int32_t *out){
 if(!s || !n || n>2048 || !out || scale<1)return -1;JsonDocument doc;
 if(deserializeJson(doc,s,n) || !doc.is<JsonObject>() || !doc[key].is<double>())return -1;
 double v=std::round(doc[key].as<double>()*scale);if(!std::isfinite(v)||v<INT32_MIN||v>INT32_MAX)return -1;*out=(int32_t)v;return 0;
}
void launch(){
 home=lv_screen_active();screen_obj=lv_obj_create(nullptr);closing=false;
 lv_obj_set_style_bg_color(screen_obj,lv_color_hex(0x172131),0);
 lv_obj_set_style_text_color(screen_obj,lv_color_white(),0);
 lv_obj_set_style_text_font(screen_obj,LV_FONT_DEFAULT,0);
 lv_obj_set_flex_flow(screen_obj,LV_FLEX_FLOW_COLUMN);
 lv_obj_set_flex_align(screen_obj,LV_FLEX_ALIGN_CENTER,LV_FLEX_ALIGN_CENTER,LV_FLEX_ALIGN_CENTER);
 native_app::ui::begin(screen_obj,dispatch);native_app::settings::begin("system-monitor-preview");
 static const gadget_host_api api={sizeof(api),1,4,log_message,nullptr,nullptr,nullptr,nullptr,timer_create,close_app,nullptr,json_number,&native_app::ui::api,setting_get,setting_set,nullptr,system_read,[]()->int32_t{return 0;},json_string};
 lifecycle={sizeof(lifecycle),1,2,nullptr};char *args[]={(char*)&api,(char*)&lifecycle};
 ui_load_screen(screen_obj,false);system_source::system_monitor_preview_entry(2,args);

}
APP_REGISTER_COLOR_RANK("System Monitor\nPreview",LV_SYMBOL_LIST,launch,0x237e77,91);
}
#endif
