#include "experimental/NativeUi.h"
#include "experimental/NativeSettings.h"
#include "AppRegistry.h"
#include "ui/TouchKeyboard.h"
#include <cassert>
#include <cstring>
#include <string>
#include <fstream>
#include <iostream>
#include <cstdlib>
#include <ctime>
void agent_usage_simulator_launch();
void system_monitor_simulator_launch();
namespace ui=native_app::ui;
namespace settings=native_app::settings;
static uint8_t frame[320*480*3],buffer[320*48*4];
static unsigned callbacks=0;static bool closing=false;
static void dispatch(gadget_callback cb,void *u){if(!closing)cb(u);}
static void clicked(void*){++callbacks;}
static lv_obj_t *find(lv_obj_t *o,const char *text){
 if(lv_obj_has_flag(o,LV_OBJ_FLAG_HIDDEN))return nullptr;
 if(lv_obj_check_type(o,&lv_label_class)&&!strcmp(lv_label_get_text(o),text))return o;
 for(unsigned i=0;i<lv_obj_get_child_count(o);++i)if(auto *r=find(lv_obj_get_child(o,i),text))return r;return nullptr;
}
static void bounds(lv_obj_t *o){if(lv_obj_has_flag(o,LV_OBJ_FLAG_HIDDEN))return;lv_area_t a;lv_obj_get_coords(o,&a);assert(a.x1>=0&&a.y1>=0&&a.x2<320&&a.y2<480);if(lv_obj_check_type(o,&lv_label_class)){lv_point_t size;lv_text_get_size(&size,lv_label_get_text(o),lv_obj_get_style_text_font(o,LV_PART_MAIN),0,0,LV_COORD_MAX,LV_TEXT_FLAG_NONE);if(lv_label_get_long_mode(o)!=LV_LABEL_LONG_DOT&&size.x>lv_obj_get_content_width(o))std::cerr<<"Clipped label: "<<lv_label_get_text(o)<<" text="<<size.x<<" box="<<lv_obj_get_content_width(o)<<"\n";assert(lv_label_get_long_mode(o)==LV_LABEL_LONG_DOT||size.x<=lv_obj_get_content_width(o));}
 for(unsigned i=0;i<lv_obj_get_child_count(o);++i)bounds(lv_obj_get_child(o,i));}
static void capture(const char *directory,const char *name){lv_obj_update_layout(lv_screen_active());bounds(lv_screen_active());lv_refr_now(nullptr);std::ofstream f(std::string(directory)+"/"+name+".ppm",std::ios::binary);f<<"P6\n320 480\n255\n";f.write((char*)frame,sizeof frame);}
int main(int argc,char **argv){assert(argc==2);lv_init();lv_tick_set_cb([](){return 1U;});auto *d=lv_display_create(320,480);lv_display_set_color_format(d,LV_COLOR_FORMAT_XRGB8888);lv_display_set_buffers(d,buffer,nullptr,sizeof buffer,LV_DISPLAY_RENDER_MODE_PARTIAL);
 lv_display_set_flush_cb(d,[](lv_display_t *d,const lv_area_t*a,uint8_t*p){int w=a->x2-a->x1+1;for(int y=a->y1;y<=a->y2;++y)for(int x=a->x1;x<=a->x2;++x){int dest=(y*320+x)*3,src=((y-a->y1)*w+x-a->x1)*4;frame[dest]=p[src+2];frame[dest+1]=p[src+1];frame[dest+2]=p[src];}lv_display_flush_ready(d);});
#if LV_USE_PERF_MONITOR
 lv_sysmon_hide_performance(d);
#endif
 auto *screen=lv_screen_active();
 // Shared built-in form behavior: switching fields transfers the steady caret.
 auto *first=lv_textarea_create(screen),*second=lv_textarea_create(screen);
 auto *keyboard=lv_keyboard_create(screen);configure_text_input(first);configure_text_input(second);
 attach_touch_keyboard(keyboard,first);
 assert(lv_obj_has_state(first,LV_STATE_FOCUSED)&&lv_obj_get_style_border_width(first,LV_PART_CURSOR)==2);
 assert(lv_obj_get_style_anim_duration(first,LV_PART_CURSOR)==0);
 attach_touch_keyboard(keyboard,second);
 assert(!lv_obj_has_state(first,LV_STATE_FOCUSED)&&lv_obj_get_style_border_width(first,LV_PART_CURSOR)==0);
 assert(lv_obj_has_state(second,LV_STATE_FOCUSED)&&lv_obj_get_style_border_width(second,LV_PART_CURSOR)==2);
 attach_touch_keyboard(keyboard,nullptr);assert(!lv_obj_has_state(second,LV_STATE_FOCUSED));
 lv_obj_clean(screen);

 for(int cycle=0;cycle<100;++cycle){ui::begin(screen,dispatch);auto root=ui::api.create(nullptr,GADGET_UI_ROOT,nullptr,nullptr,nullptr);assert(root);assert(!ui::api.create(nullptr,GADGET_UI_ROOT,nullptr,nullptr,nullptr));assert(!ui::api.create((void*)123,GADGET_UI_LABEL,"bad",nullptr,nullptr));
 auto graph=ui::api.create(root,GADGET_UI_CHART,nullptr,nullptr,nullptr);int32_t points[]={0,-1,100};assert(ui::api.chart(graph,points,3)==0);assert(ui::api.chart(graph,points,61)==-1);points[0]=101;assert(ui::api.chart(graph,points,3)==-1);assert(ui::api.opacity(graph,101)==-1);
 auto card=ui::api.create(root,GADGET_UI_CONTAINER,nullptr,nullptr,nullptr);auto label=ui::api.create(card,GADGET_UI_LABEL,"test",nullptr,nullptr);auto bar=ui::api.create(card,GADGET_UI_BAR,nullptr,nullptr,nullptr);assert(ui::api.value(bar,101)==-1);assert(ui::api.value(bar,72)==0);assert(ui::api.value(label,1)==-1);assert(ui::api.text(label,std::string(161,'x').c_str())==-1);
 auto button=ui::api.create(card,GADGET_UI_BUTTON,"click",clicked,nullptr);assert(button);auto *caption=find(screen,"click");assert(caption);
 auto *button_obj=lv_obj_get_parent(caption);lv_obj_update_layout(screen);
 assert(ui::api.style(button,GADGET_STYLE_DISABLED)==0&&lv_obj_has_state(button_obj,LV_STATE_DISABLED));assert(ui::api.style(button,GADGET_STYLE_ENABLED)==0&&!lv_obj_has_state(button_obj,LV_STATE_DISABLED));
 assert(ui::api.style(label,GADGET_STYLE_MARQUEE)==0&&lv_label_get_long_mode(find(screen,"test"))==LV_LABEL_LONG_SCROLL_CIRCULAR);
 assert(ui::api.style(graph,GADGET_STYLE_CHART_SMOOTH)==0);points[0]=0;assert(ui::api.chart_fit(graph,points,3)==0);auto*chart=lv_obj_get_child(screen,0);assert(lv_obj_check_type(chart,&lv_chart_class));auto*series=lv_chart_get_series_next(chart,nullptr);auto*values=lv_chart_get_series_y_array(chart,series);unsigned point_count=lv_chart_get_point_count(chart);assert(point_count>3&&point_count<=289&&values[0]==80&&values[point_count-1]==920);for(unsigned j=1;j+1<point_count;++j)assert(values[j]==LV_CHART_POINT_NONE);

 const auto normal=lv_obj_get_style_bg_color(button_obj,LV_PART_MAIN);
 lv_obj_add_state(button_obj,LV_STATE_PRESSED);
 assert(!lv_color_eq(normal,lv_obj_get_style_bg_color(button_obj,LV_PART_MAIN)));
 lv_obj_remove_state(button_obj,LV_STATE_PRESSED);
 assert(lv_color_eq(normal,lv_obj_get_style_bg_color(button_obj,LV_PART_MAIN)));
 lv_obj_send_event(lv_obj_get_parent(caption),LV_EVENT_CLICKED,nullptr);assert(callbacks==(unsigned)cycle+1);
 ui::suspend();assert(ui::api.text(label,"closed")==-1);lv_obj_send_event(lv_obj_get_parent(caption),LV_EVENT_CLICKED,nullptr);assert(callbacks==(unsigned)cycle+1);ui::end();assert(lv_obj_get_child_count(screen)==0);assert(ui::api.visible(card,1)==-1);
 }
 ui::begin(screen,dispatch);auto root=ui::api.create(nullptr,GADGET_UI_ROOT,nullptr,nullptr,nullptr);for(int i=0;i<63;++i)assert(ui::api.create(root,GADGET_UI_LABEL,"",nullptr,nullptr));assert(!ui::api.create(root,GADGET_UI_LABEL,"",nullptr,nullptr));ui::end();
 settings::begin("test-app");assert(settings::set("mode","used")==0);char value[256];assert(settings::get("mode",value,3)==-1);assert(settings::get("mode",value,sizeof value)==4&&!strcmp(value,"used"));assert(settings::set("../bad","x")==-1);assert(settings::set("large",std::string(256,'x').c_str())==-1);
 for(int i=0;i<7;++i)assert(settings::set(("key"+std::to_string(i)).c_str(),"v")==0);assert(settings::set("overflow","v")==-1);settings::end();assert(settings::get("mode",value,sizeof value)==-1);settings::begin("other-app");assert(settings::get("mode",value,sizeof value)==-1);settings::begin("test-app");assert(settings::get("mode",value,sizeof value)==4);settings::end();
 auto launch=agent_usage_simulator_launch;
 auto usage_tab=[&](){auto *label=find(lv_screen_active(),"Usage");assert(label);lv_obj_send_event(lv_obj_get_parent(label),LV_EVENT_CLICKED,nullptr);};
 launch();capture(argv[1],"agent-dashboard-activity");assert(find(lv_screen_active(),"Needs attention"));
 auto *session=find(lv_screen_active(),"gadget-os");assert(session);lv_obj_send_event(lv_obj_get_parent(session),LV_EVENT_CLICKED,nullptr);capture(argv[1],"agent-dashboard-session");assert(find(lv_screen_active(),"Task details"));
 usage_tab();capture(argv[1],"agent-usage-remaining");assert(find(lv_screen_active(),"72%"));
 auto *codex=find(lv_screen_active(),"Codex >");assert(codex);lv_obj_send_event(lv_obj_get_parent(codex),LV_EVENT_CLICKED,nullptr);
 capture(argv[1],"agent-usage-codex-details");assert(find(lv_screen_active(),"12.4M"));assert(find(lv_screen_active(),"8d"));assert(!find(lv_screen_active(),"Remaining"));
 auto *today=find(lv_screen_active(),"Today");assert(today);lv_obj_send_event(lv_obj_get_parent(today),LV_EVENT_CLICKED,nullptr);capture(argv[1],"agent-usage-today");assert(find(lv_screen_active(),"66K tokens / UTC"));
 auto *month=find(lv_screen_active(),"Month");assert(month);lv_obj_send_event(lv_obj_get_parent(month),LV_EVENT_CLICKED,nullptr);capture(argv[1],"agent-usage-month");assert(find(lv_screen_active(),"750K tokens / UTC"));
 auto *details_back=find(lv_screen_active(),LV_SYMBOL_LEFT);lv_obj_send_event(lv_obj_get_parent(details_back),LV_EVENT_CLICKED,nullptr);assert(find(lv_screen_active(),"Remaining"));assert(!find(lv_screen_active(),"Used"));assert(find(lv_screen_active(),"72%"));
 auto *back=find(lv_screen_active(),LV_SYMBOL_LEFT);assert(back);lv_obj_send_event(lv_obj_get_parent(back),LV_EVENT_CLICKED,nullptr);lv_timer_handler();launch();usage_tab();lv_obj_update_layout(lv_screen_active());assert(find(lv_screen_active(),"72%"));
 back=find(lv_screen_active(),LV_SYMBOL_LEFT);lv_obj_send_event(lv_obj_get_parent(back),LV_EVENT_CLICKED,nullptr);lv_timer_handler();
 {std::ofstream f("snapshot.json");f<<R"({"schema":1,"codex_enabled":1,"claude_enabled":0,"codex_age":0,"claude_age":-1,"codex_primary_used":28,"codex_primary_minutes":300,"codex_primary_reset":600,"codex_secondary_used":-1,"codex_detail_age":0,"codex_d5_v":120,"codex_d5_u":1,"codex_d5_date":1113,"cx_p0_n":0,"cx_p0_e":1})";}
 setenv("GADGET_USAGE_SNAPSHOT","snapshot.json",1);launch();usage_tab();codex=find(lv_screen_active(),"Codex >");lv_obj_send_event(lv_obj_get_parent(codex),LV_EVENT_CLICKED,nullptr);today=find(lv_screen_active(),"Today");lv_obj_send_event(lv_obj_get_parent(today),LV_EVENT_CLICKED,nullptr);capture(argv[1],"agent-usage-today-unreported");assert(find(lv_screen_active(),"Today not reported by Codex"));assert(find(lv_screen_active(),"Latest report: 11/13 UTC"));
 back=find(lv_screen_active(),LV_SYMBOL_LEFT);lv_obj_send_event(lv_obj_get_parent(back),LV_EVENT_CLICKED,nullptr);back=find(lv_screen_active(),LV_SYMBOL_LEFT);lv_obj_send_event(lv_obj_get_parent(back),LV_EVENT_CLICKED,nullptr);lv_timer_handler();
 {std::ofstream f("snapshot.json");f<<R"({"schema":1,"codex_enabled":1,"claude_enabled":0,"codex_age":600,"claude_age":-1,"codex_primary_used":35,"codex_primary_minutes":300,"codex_primary_reset":600,"codex_secondary_used":-1})";}
 setenv("GADGET_USAGE_SNAPSHOT","snapshot.json",1);launch();usage_tab();capture(argv[1],"agent-usage-single-stale");assert(find(lv_screen_active(),"65%")&&find(lv_screen_active(),"Stale"));assert(!find(lv_screen_active(),"Claude"));
 back=find(lv_screen_active(),LV_SYMBOL_LEFT);lv_obj_send_event(lv_obj_get_parent(back),LV_EVENT_CLICKED,nullptr);lv_timer_handler();
 {std::ofstream f("snapshot.json");f<<R"({"schema":1,"generated_at":1,"codex_enabled":1,"claude_enabled":0,"codex_age":0,"claude_age":-1,"codex_primary_used":35,"codex_primary_minutes":300,"codex_primary_reset":600,"codex_secondary_used":-1})";}
 launch();usage_tab();lv_obj_update_layout(lv_screen_active());assert(find(lv_screen_active(),"--")&&find(lv_screen_active(),"Stale"));assert(!find(lv_screen_active(),"65%"));
 back=find(lv_screen_active(),LV_SYMBOL_LEFT);lv_obj_send_event(lv_obj_get_parent(back),LV_EVENT_CLICKED,nullptr);lv_timer_handler();
 {std::ofstream f("snapshot.json");f<<"{}";}launch();usage_tab();capture(argv[1],"agent-usage-error");assert(find(lv_screen_active(),"Refresh failed - tap refresh"));
 back=find(lv_screen_active(),LV_SYMBOL_LEFT);lv_obj_send_event(lv_obj_get_parent(back),LV_EVENT_CLICKED,nullptr);lv_timer_handler();
 {std::ofstream f("activity.json");f<<"{\"schema\":1,\"generated_at\":"<<std::time(nullptr)<<",\"age\":0,\"enabled\":1,\"count\":0,\"total\":0,\"attention\":0}";}
 setenv("GADGET_ACTIVITY_SNAPSHOT","activity.json",1);launch();
 auto *activity=find(lv_screen_active(),"Activity");assert(activity);lv_obj_send_event(lv_obj_get_parent(activity),LV_EVENT_CLICKED,nullptr);capture(argv[1],"agent-dashboard-empty");assert(find(lv_screen_active(),"Companion connected"));
 back=find(lv_screen_active(),LV_SYMBOL_LEFT);lv_obj_send_event(lv_obj_get_parent(back),LV_EVENT_CLICKED,nullptr);lv_timer_handler();
 {std::ofstream f("activity.json");f<<R"({"schema":1,"generated_at":1,"age":0,"enabled":1,"count":0,"total":0,"attention":0})";}
 launch();activity=find(lv_screen_active(),"Activity");lv_obj_send_event(lv_obj_get_parent(activity),LV_EVENT_CLICKED,nullptr);capture(argv[1],"agent-dashboard-offline");assert(find(lv_screen_active(),"Waiting for companion"));
 back=find(lv_screen_active(),LV_SYMBOL_LEFT);lv_obj_send_event(lv_obj_get_parent(back),LV_EVENT_CLICKED,nullptr);lv_timer_handler();unsetenv("GADGET_ACTIVITY_SNAPSHOT");
 auto system_launch=system_monitor_simulator_launch;system_launch();capture(argv[1],"system-monitor-overview");assert(find(lv_screen_active(),"38%"));
 auto click_text=[&](const char*t){auto*l=find(lv_screen_active(),t);assert(l);lv_obj_send_event(lv_obj_get_parent(l),LV_EVENT_CLICKED,nullptr);};
 click_text("CPU");capture(argv[1],"system-monitor-cores");assert(find(lv_screen_active(),"Core 8"));assert(find(lv_screen_active(),"100%"));click_text(LV_SYMBOL_LEFT);assert(find(lv_screen_active(),"38%"));
 click_text("Processes");capture(argv[1],"system-monitor-processes");assert(find(lv_screen_active(),"14.2%"));click_text("Memory");capture(argv[1],"system-monitor-memory");assert(find(lv_screen_active(),"1.6 GB"));
 click_text("Storage");capture(argv[1],"system-monitor-storage");assert(find(lv_screen_active(),"184 GB free"));click_text(LV_SYMBOL_LEFT);lv_timer_handler();
 {std::ofstream f("system.json");f<<R"({"schema":1,"age":0,"host":"My MacBook","cpu":38,"cores":8,"ram":84,"ram10":837,"mem_mib":13713,"total_mib":16384,"ram_hist":"545454545454535353535353","ram_frac":"224466998877"})";}setenv("GADGET_SYSTEM_SNAPSHOT","system.json",1);system_launch();capture(argv[1],"system-monitor-memory-precision");assert(find(lv_screen_active(),"83.7%"));assert(find(lv_screen_active(),"82-86% / 2 min"));click_text(LV_SYMBOL_LEFT);lv_timer_handler();
 {std::ofstream f("system.json");f<<R"({"schema":1,"age":11,"host":"A very long computer name that should truncate","cpu":38,"ram":68,"ct0_name":"A very long process name that should truncate","ct0_value":125})";}setenv("GADGET_SYSTEM_SNAPSHOT","system.json",1);system_launch();capture(argv[1],"system-monitor-offline");assert(find(lv_screen_active(),"Offline"));click_text("Processes");capture(argv[1],"system-monitor-long-name");click_text(LV_SYMBOL_LEFT);lv_timer_handler();
 {std::ofstream f("system.json");f<<R"({"schema":1,"age":0,"cpu":100,"ram":100,"ram10":1000,"cores":4096,"total_mib":2147483647,"mem_mib":2147483647,"down_bps":2147483647,"up_bps":2147483647,"battery":100,"power":1,"uptime":2147483647,"disk_total_mib":2147483647,"disk_free_mib":0})";}system_launch();capture(argv[1],"system-monitor-large-values");click_text("Storage");capture(argv[1],"system-monitor-full-disk");click_text(LV_SYMBOL_LEFT);lv_timer_handler();
 std::cout<<"PASS: shared UI bounds, real click events, widget/text quotas, invalid handles, close gating, 100 cleanup cycles, settings isolation/persistence, rendered remaining overview and Today/7 days/Month details\n";
}
