#include "experimental/NativeApp.h"
#include "experimental/NativeSimulator.h"
#include "experimental/NativeStore.h"
#include "apps/SettingsApp.h"
#include "services/ConfigStoreService.h"
#include "services/TimeService.h"
#include "ui/LauncherOrder.h"
#include "ui/I18n.h"
#include "ui/UiNav.h"
#include <cassert>
#include <cstring>
#include <iostream>
#include <fstream>
#include <sys/stat.h>
#include "../../experiments/native-app/sdk/gadget_app.h"
extern "C" {
#define gadget_app_entry settings_demo_entry
#include "../../experiments/native-app/example/settings_demo.c"
#undef gadget_app_entry
}
#include "experimental/NativeUi.h"
#include "experimental/NativeSettings.h"
static uint32_t ticks=1;
static uint8_t frame[320*480*3];
static void capture(const char *name){
 if(const char *directory=std::getenv("LAUNCHER_PREVIEW_DIR")){
  lv_obj_update_layout(lv_screen_active());lv_refr_now(nullptr);
  std::ofstream f(std::string(directory)+"/"+name+".ppm",std::ios::binary);
  f<<"P6\n320 480\n255\n";f.write(reinterpret_cast<char *>(frame),sizeof(frame));
 }
}
static lv_obj_t *caller;
static gadget_app_lifecycle lifecycle;
static gadget_host_api api;
static unsigned launches=0;
static lv_obj_t *find(lv_obj_t *o,const char *text){
 if(lv_obj_check_type(o,&lv_label_class)&&!strcmp(lv_label_get_text(o),text))return o;
 for(unsigned i=0;i<lv_obj_get_child_count(o);++i)if(auto *r=find(lv_obj_get_child(o,i),text))return r;
 return nullptr;
}
static void settle(){ticks+=50;lv_timer_handler();}
static void click(const char *text){auto *label=find(lv_screen_active(),text);assert(label);lv_obj_send_event(lv_obj_get_parent(label),LV_EVENT_CLICKED,nullptr);settle();}
static void close_demo(){lv_async_call([](void*){
 native_app::ui::end();native_app::settings::end();lifecycle.shutdown();ui_load_screen(caller,true);
 },nullptr);}
static void demo(bool settings){
 ++launches;caller=lv_screen_active();lv_screen_load(lv_obj_create(nullptr));
 native_app::ui::begin(lv_screen_active(),[](gadget_callback cb,void *u){cb(u);});
 native_app::settings::begin("settings-demo-preview");
 api={};api.struct_size=sizeof(api);api.major=1;api.minor=8;api.ui=&native_app::ui::api;
 api.setting_get=native_app::settings::get;api.setting_set=native_app::settings::set;
 api.launch_mode=settings?+[]()->uint32_t{return GADGET_LAUNCH_SETTINGS;}:+[]()->uint32_t{return GADGET_LAUNCH_NORMAL;};
 api.request_close=close_demo;lifecycle={sizeof(lifecycle),1,8,nullptr};
 char *args[]={reinterpret_cast<char *>(&api),reinterpret_cast<char *>(&lifecycle)};
 assert(settings_demo_entry(2,args)==0);
}
static void settings_adapter(){demo(true);}
int main(){
 using namespace launcher;
 assert(page_count(0)==1&&page_count(9)==1&&page_count(10)==2&&page_count(19)==3);
 std::vector<std::string> available={"builtin:Weather","native:stocks","native:transit"};
 assert(reconcile("native:stocks\nnative:removed\nnative:stocks",available)==std::vector<std::string>({"native:stocks","builtin:Weather","native:transit"}));
 assert(reconcile(serialize(available),available)==available);
 assert(ConfigStoreService::instance().begin());TimeService::instance().begin();
 lv_init();lv_tick_set_cb([](){return ticks;});auto *display=lv_display_create(320,480);
 static uint8_t buffer[320*40*4];lv_display_set_buffers(display,buffer,nullptr,sizeof(buffer),LV_DISPLAY_RENDER_MODE_PARTIAL);
 lv_display_set_color_format(display,LV_COLOR_FORMAT_XRGB8888);
 lv_display_set_flush_cb(display,[](lv_display_t *d,const lv_area_t *a,uint8_t *p){
  for(int y=a->y1;y<=a->y2;++y)for(int x=a->x1;x<=a->x2;++x){int src=((y-a->y1)*(a->x2-a->x1+1)+x-a->x1)*4,dst=(y*320+x)*3;frame[dst]=p[src+2];frame[dst+1]=p[src+1];frame[dst+2]=p[src];}
  lv_display_flush_ready(d);
 });i18n::init();i18n::apply_fonts(display);
 native_launcher_restore();settle();
 auto *pager=lv_obj_get_child(lv_screen_active(),1);
 assert(lv_obj_get_child_count(pager)==1); // Built-ins fit on one page before migration.
 assert(!find(lv_screen_active(),"Installed apps"));
 std::string error;auto catalog=native_app::simulator::catalog();
 for(const auto &item:catalog)assert(native_app::simulator::install(item,error));
 native_launcher_restore();settle();pager=lv_obj_get_child(lv_screen_active(),1);
 capture("launcher-mixed");
 assert(lv_obj_get_child_count(pager)==2);
 assert(lv_obj_get_child_count(lv_obj_get_child(lv_obj_get_child(pager,0),0))==page_capacity);
 for(unsigned page=0;page<lv_obj_get_child_count(pager);++page){
   auto *grid=lv_obj_get_child(lv_obj_get_child(pager,page),0);
   assert(lv_obj_get_child_count(grid)<=page_capacity);
 }
 auto &cfg=ConfigStoreService::instance();assert(cfg.load("launcher"));auto order_before=cfg.get("order","");
 assert(native_app::simulator::install(catalog.front(),error));native_launcher_restore();settle();
 assert(cfg.load("launcher")&&cfg.get("order","")==order_before);
 // Reorder through the real Settings UI, including a page-boundary move.
 std::vector<std::string> expected;size_t begin=0;while(begin<order_before.size()){auto end=order_before.find('\n',begin);expected.push_back(order_before.substr(begin,end-begin));if(end==std::string::npos)break;begin=end+1;}
 settings_launch();settle();click("Arrange apps");capture("arrange-apps");
 auto arrange_list=lv_obj_get_child(lv_screen_active(),1);
 auto arrange_row=[&](size_t index){return lv_obj_get_child(arrange_list,index+1);};
 auto move=[&](size_t index,const char *direction){auto *label=find(arrange_row(index),direction);assert(label);lv_obj_send_event(lv_obj_get_parent(label),LV_EVENT_CLICKED,nullptr);settle();};
 assert(lv_obj_has_state(lv_obj_get_parent(find(arrange_row(0),LV_SYMBOL_UP)),LV_STATE_DISABLED));
 assert(lv_obj_has_state(lv_obj_get_parent(find(arrange_row(expected.size()-1),LV_SYMBOL_DOWN)),LV_STATE_DISABLED));
 move(0,LV_SYMBOL_DOWN);std::swap(expected[0],expected[1]);
 assert(cfg.load("launcher")&&cfg.get("order","")==launcher::serialize(expected));assert(find(arrange_row(1),"2. Add apps"));
 move(8,LV_SYMBOL_DOWN);std::swap(expected[8],expected[9]);
 assert(cfg.load("launcher")&&cfg.get("order","")==launcher::serialize(expected));capture("arrange-apps-page-move");
 // Failed persistence leaves the displayed order untouched and reports it.
 std::string path=std::string(getenv("GADGET_OS_DATA_DIR"))+"/config/launcher.json",backup=path+".bak";
 assert(std::rename(path.c_str(),backup.c_str())==0);assert(mkdir(path.c_str(),0700)==0);
 move(1,LV_SYMBOL_UP);assert(find(lv_screen_active(),"Could not save settings"));assert(find(arrange_row(1),"2. Add apps"));
 assert(std::remove(path.c_str())==0);assert(std::rename(backup.c_str(),path.c_str())==0);
 click(LV_SYMBOL_LEFT);click(LV_SYMBOL_LEFT);settle();assert(native_launcher_is_active());
 pager=lv_obj_get_child(lv_screen_active(),1);auto *first_grid=lv_obj_get_child(lv_obj_get_child(pager,0),0);
 assert(find(lv_obj_get_child(first_grid,0),"Weather"));assert(find(lv_obj_get_child(first_grid,1),"Add apps"));
 assert(lv_obj_get_child_count(first_grid)==page_capacity);
 settings_launch_with_route("settings_arrange_apps");settle();arrange_list=lv_obj_get_child(lv_screen_active(),1);
 assert(find(arrange_row(1),"2. Add apps"));move(1,LV_SYMBOL_UP);std::swap(expected[0],expected[1]);
 move(9,LV_SYMBOL_UP);std::swap(expected[8],expected[9]);assert(launcher::serialize(expected)==order_before);
 click(LV_SYMBOL_LEFT);click(LV_SYMBOL_LEFT);settle();assert(native_launcher_is_active());
 assert(!native_app::launch_settings_id("agent-usage"));
 settings_launch_with_route("settings_apps");settle();
 assert(settings_apps_active()&&find(lv_screen_active(),"Stocks"));
 assert(!native_app::launch_settings_id("agent-usage"));click(LV_SYMBOL_LEFT);
 assert(!settings_apps_active()&&find(lv_screen_active(),"Settings"));click(LV_SYMBOL_LEFT);
 native_app::simulator::register_settings_adapter("agent-usage",settings_adapter);
 settings_launch();settle();click("Apps");
 assert(settings_apps_active()&&find(lv_screen_active(),"Agent Dashboard"));assert(launches==0);capture("app-settings-list");
 click("Agent Dashboard");native_app::poll();settle();
 assert(launches==1&&find(lv_screen_active(),"Demo settings"));
 click("Toggle");assert(find(lv_screen_active(),"Enabled"));capture("app-settings-demo");click("Back");native_app::poll();
 assert(settings_apps_active()&&!native_app::busy());
 click("Agent Dashboard");native_app::poll();settle();assert(find(lv_screen_active(),"Enabled"));
 click("Back");native_app::poll();click(LV_SYMBOL_LEFT);click(LV_SYMBOL_LEFT);
 demo(false);settle();click("Settings");assert(find(lv_screen_active(),"Enabled"));click("Back");assert(find(lv_screen_active(),"Settings demo"));click("Close");
 assert(native_launcher_is_active());
 pager=lv_obj_get_child(lv_screen_active(),1);lv_obj_update_layout(pager);
 lv_obj_scroll_to_x(pager,lv_obj_get_width(pager),LV_ANIM_OFF);
 native_launcher_release();native_launcher_restore();settle();
 capture("launcher-second-page");
 for(const auto &item:catalog)assert(native_app::store::uninstall(item.id,error));
 native_launcher_restore();settle();pager=lv_obj_get_child(lv_screen_active(),1);assert(lv_obj_get_child_count(pager)==1);assert(lv_obj_get_scroll_x(pager)==0);
 // Exercise the device's release/recreate path from an app-settings list.
 for(int i=0;i<20;++i){settings_launch_with_route("settings_apps");settle();native_launcher_release();native_launcher_restore();settle();assert(native_launcher_is_active());}
 std::cout<<"PASS: unified pagination, stable order, lazy settings discovery, API 1.8 fixture, persistent settings and both Back origins\n";
}
