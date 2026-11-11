#include "experimental/NativeApp.h"
#include "experimental/NativeAppStore.h"
#include "experimental/NativeStore.h"
#include "experimental/NativeSimulator.h"
#include "ui/I18n.h"
#include <cassert>
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>

void apply_display_preferences(){}
static lv_obj_t *home;
static unsigned char frame[320*480*3],buffer[320*48*4];
static lv_obj_t *find(lv_obj_t *obj,const char *text){
    if(lv_obj_check_type(obj,&lv_label_class)&&!strcmp(lv_label_get_text(obj),text))return obj;
    for(unsigned i=0;i<lv_obj_get_child_count(obj);++i)if(auto *result=find(lv_obj_get_child(obj,i),text))return result;
    return nullptr;
}
static void click(const char *text){
    auto *label=find(lv_screen_active(),text);assert(label);
    lv_obj_send_event(lv_obj_get_parent(label),LV_EVENT_CLICKED,nullptr);
    native_app::poll();native_app::poll();lv_obj_update_layout(lv_screen_active());
}
bool native_launcher_is_active(){return home&&lv_screen_active()==home;}
void native_launcher_release(){auto *old=lv_screen_active();home=nullptr;lv_screen_load(lv_obj_create(nullptr));lv_obj_delete(old);}
void native_launcher_restore(){
    auto *old=lv_screen_active();home=lv_obj_create(nullptr);lv_screen_load(home);lv_obj_delete(old);
    lv_obj_set_flex_flow(home,LV_FLEX_FLOW_ROW_WRAP);
    native_app::app_store::create_tile(home);native_app::store::create_tiles(home);
}
static void capture(const char *directory,const char *name){
    lv_obj_update_layout(lv_screen_active());lv_refr_now(nullptr);
    std::ofstream f(std::string(directory)+"/"+name+".ppm",std::ios::binary);
    f<<"P6\n320 480\n255\n";f.write((char*)frame,sizeof frame);
}
int main(int argc,char **argv){
    assert(argc==3);lv_init();lv_tick_set_cb([](){return 1U;});auto *d=lv_display_create(320,480);
    lv_display_set_color_format(d,LV_COLOR_FORMAT_XRGB8888);lv_display_set_buffers(d,buffer,nullptr,sizeof buffer,LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(d,[](lv_display_t *d,const lv_area_t *a,uint8_t *p){
        int w=a->x2-a->x1+1;
        for(int y=a->y1;y<=a->y2;++y)for(int x=a->x1;x<=a->x2;++x){int dest=(y*320+x)*3,src=((y-a->y1)*w+x-a->x1)*4;frame[dest]=p[src+2];frame[dest+1]=p[src+1];frame[dest+2]=p[src];}
        lv_display_flush_ready(d);
    });
    i18n::init();i18n::apply_fonts(d);lv_sysmon_hide_performance(d);
    native_launcher_restore();
    if(std::string(argv[2])=="restart"){
        assert(native_app::store::installed().size()==1);assert(find(home,"System Monitor"));
        click("Add apps");click("Installed apps");click("System Monitor");click("Remove");
        assert(native_app::store::installed().empty());native_app::app_store::close();native_app::poll();
        assert(!find(home,"System Monitor"));std::cout<<"PASS: persistence across processes and removal\n";return 0;
    }
    assert(native_app::store::installed().empty());
    click("Add apps");assert(find(lv_screen_active(),"Agent Dashboard"));capture(argv[1],"store-catalog");
    click("System Monitor");assert(find(lv_screen_active(),"Install app"));
    assert(find(lv_screen_active(),"Requires Gadget Companion on your computer"));capture(argv[1],"store-details");
    click("Set up computer");assert(find(lv_screen_active(),"Connect computer"));capture(argv[1],"companion-setup");
    click("Back");assert(native_launcher_is_active());click("Add apps");click("System Monitor");
    click("Install app");assert(native_app::store::installed().size()==1);assert(find(lv_screen_active(),"Open app"));
    capture(argv[1],"store-installed");click("Open app");assert(native_app::running("system-monitor"));
    assert(find(lv_screen_active(),"Overview"));click(LV_SYMBOL_LEFT);lv_timer_handler();native_app::poll();
    assert(native_launcher_is_active());assert(find(home,"System Monitor"));
    click("System Monitor");assert(native_app::running("system-monitor"));click(LV_SYMBOL_LEFT);lv_timer_handler();native_app::poll();
    click("Add apps");click("Installed apps");click("System Monitor");click("Cancel");assert(native_app::store::installed().size()==1);
    click("System Monitor");capture(argv[1],"store-remove");click("Remove");assert(native_app::store::installed().empty());
    native_app::app_store::close();native_app::poll();assert(!find(home,"System Monitor"));
    // Repeated lifecycle exercises rebuilding the launcher and deleting tile callbacks.
    for(int i=0;i<10;++i){
        click("Add apps");click("System Monitor");click("Install app");click("Remove");click("Remove");
        native_app::app_store::close();native_app::poll();assert(native_app::store::installed().empty());
    }
    // Every catalog entry can be installed and removed through the shared UI.
    for(const auto &item:native_app::simulator::catalog()){
        click("Add apps");click(item.name.c_str());click("Install app");
        native_app::app_store::close();native_app::poll();assert(find(home,item.name.c_str()));
        click("Add apps");click("Installed apps");click(item.name.c_str());click("Remove");
        native_app::app_store::close();native_app::poll();assert(!find(home,item.name.c_str()));
    }
    click("Add apps");click("System Monitor");click("Install app");native_app::app_store::close();native_app::poll();
    assert(find(home,"System Monitor"));std::cout<<"PASS: shared store UI install/open/remove/cancel and repeated lifecycle\n";
}
