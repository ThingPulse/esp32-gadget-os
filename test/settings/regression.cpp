#include <cassert>
#include <cstdlib>
#include <fstream>
#include <sys/stat.h>
#include "../../src/apps/SettingsApp.cpp"
#include "src/widgets/buttonmatrix/lv_buttonmatrix_private.h"
static uint32_t tick=0;static uint8_t frame[320*480*3];
static lv_obj_t *find_label(lv_obj_t *root,const char *text){
 if(lv_obj_check_type(root,&lv_label_class)&&std::string(lv_label_get_text(root))==text)return root;
 for(uint32_t i=0;i<lv_obj_get_child_count(root);++i)if(auto *label=find_label(lv_obj_get_child(root,i),text))return label;
 return nullptr;
}
static void click(lv_obj_t *root,const char *text){auto *label=find_label(root,i18n::tr(text));assert(label);lv_obj_send_event(lv_obj_get_parent(label),LV_EVENT_CLICKED,nullptr);}
static void settle(){tick+=50;lv_timer_handler();}
static void capture(const char *name){lv_obj_add_flag(lv_layer_sys(),LV_OBJ_FLAG_HIDDEN);settle();lv_obj_update_layout(lv_screen_active());lv_refr_now(nullptr);if(auto *dir=getenv("SETTINGS_PREVIEW_DIR")){std::ofstream f(std::string(dir)+"/"+name+".ppm",std::ios::binary);f<<"P6\n320 480\n255\n";f.write(reinterpret_cast<char*>(frame),sizeof frame);}}
static lv_indev_t *pointer;static lv_point_t point;static bool pressed=false;
static void touch(int x,int y,bool down){point={x,y};pressed=down;tick+=30;lv_indev_read(pointer);}
static lv_point_t key_center(lv_obj_t *kb,int id){lv_obj_update_layout(kb);auto area=reinterpret_cast<lv_buttonmatrix_t*>(kb)->button_areas[id];lv_area_t box;lv_obj_get_coords(kb,&box);return {box.x1+(area.x1+area.x2)/2,box.y1+(area.y1+area.y2)/2};}
int main(){
 auto &cfg=ConfigStoreService::instance();assert(cfg.begin());assert(TimeService::instance().begin());
 lv_init();lv_tick_set_cb([](){return tick;});auto *display=lv_display_create(320,480);i18n::init();i18n::apply_fonts(display);
 static uint8_t buffer[320*40*4];lv_display_set_color_format(display,LV_COLOR_FORMAT_XRGB8888);lv_display_set_buffers(display,buffer,nullptr,sizeof buffer,LV_DISPLAY_RENDER_MODE_PARTIAL);
 lv_display_set_flush_cb(display,[](lv_display_t *d,const lv_area_t *a,uint8_t *p){for(int y=a->y1;y<=a->y2;++y)for(int x=a->x1;x<=a->x2;++x){int src=((y-a->y1)*(a->x2-a->x1+1)+x-a->x1)*4,dst=(y*320+x)*3;frame[dst]=p[src+2];frame[dst+1]=p[src+1];frame[dst+2]=p[src];}lv_display_flush_ready(d);});
 pointer=lv_indev_create();lv_indev_set_type(pointer,LV_INDEV_TYPE_POINTER);lv_indev_set_read_cb(pointer,[](lv_indev_t*,lv_indev_data_t *data){data->point=point;data->state=pressed?LV_INDEV_STATE_PRESSED:LV_INDEV_STATE_RELEASED;});
 auto *home=lv_screen_active();settings_launch();auto *settings=lv_screen_active();route_restore_settings_screen=settings;
 open_time_screen(nullptr);auto *screen=lv_screen_active();assert(!find_label(screen,"Save & Back"));
 lv_obj_remove_state(time_ctx.fmt_switch,LV_STATE_CHECKED);lv_obj_send_event(time_ctx.fmt_switch,LV_EVENT_VALUE_CHANGED,nullptr);assert(lv_screen_active()==screen);assert(cfg.load("time")&&!cfg.get_bool("use_24h",true));
 lv_roller_set_selected(time_ctx.region_roller,1,LV_ANIM_OFF);lv_obj_send_event(time_ctx.region_roller,LV_EVENT_VALUE_CHANGED,nullptr);
 lv_roller_set_selected(time_ctx.timezone_roller,1,LV_ANIM_OFF);lv_obj_send_event(time_ctx.timezone_roller,LV_EVENT_VALUE_CHANGED,nullptr);assert(cfg.load("time")&&cfg.get_int("timezone_index",-1)==time_ctx.selected_tz_index);
 tick+=1600;capture("settings-time");ui_load_screen(settings,true);settle();
 open_display_screen(nullptr);screen=lv_screen_active();assert(!find_label(screen,"Save & Back"));
 lv_slider_set_value(display_ctx.brightness_slider,42,LV_ANIM_OFF);lv_obj_send_event(display_ctx.brightness_slider,LV_EVENT_VALUE_CHANGED,nullptr);
 assert(cfg.load("display")&&cfg.get_int("brightness_percent",100)!=42);
 lv_obj_send_event(display_ctx.brightness_slider,LV_EVENT_RELEASED,nullptr);assert(lv_screen_active()==screen);assert(cfg.load("display")&&cfg.get_int("brightness_percent",0)==42);
 lv_obj_add_state(display_ctx.fps_switch,LV_STATE_CHECKED);lv_obj_send_event(display_ctx.fps_switch,LV_EVENT_VALUE_CHANGED,nullptr);assert(cfg.load("display")&&cfg.get_bool("show_fps",false));
 tick+=1600;capture("settings-display");ui_load_screen(settings,true);settle();
 // App-owned Planes fields are exercised in test/native_app/phase3.cpp.
 assert(!find_label(settings,i18n::tr(LV_SYMBOL_GPS "  Planes")));
 open_wifi_screen(nullptr);screen=lv_screen_active();lv_textarea_set_text(wifi_ctx.ssid_input,"Test network");assert(form_dirty());assert(confirm_form_back());click(settings_form.dialog,"Discard");settle();assert(lv_screen_active()==settings);assert(cfg.load("wifi")&&cfg.get("ssid","").empty());
 open_wifi_screen(nullptr);screen=lv_screen_active();lv_textarea_set_text(wifi_ctx.ssid_input,"");assert(!save_wifi_form());
 // Real pointer input: preview on press, one character on release, slide-off cancels.
 lv_obj_add_flag(wifi_ctx.content,LV_OBJ_FLAG_HIDDEN); // Isolate keyboard from the form's scrolling.
 auto *kb=wifi_ctx.kb;lv_obj_remove_flag(kb,LV_OBJ_FLAG_HIDDEN);lv_keyboard_set_textarea(kb,wifi_ctx.pass_input);lv_obj_update_layout(screen);
 auto q=key_center(kb,0),w=key_center(kb,1);auto area=reinterpret_cast<lv_buttonmatrix_t*>(kb)->button_areas[0];assert(area.x2-area.x1+1>=28&&area.y2-area.y1+1>=50);
 tick+=5100;settle();touch(q.x,q.y,true);tick+=800;lv_indev_read(pointer);assert(std::string(lv_textarea_get_text(wifi_ctx.pass_input)).empty());capture("keyboard-pressed");
 touch(q.x,q.y,false);assert(std::string(lv_textarea_get_text(wifi_ctx.pass_input))=="q");
 touch(q.x,q.y,true);touch(w.x,w.y,true);touch(w.x,w.y,false);assert(std::string(lv_textarea_get_text(wifi_ctx.pass_input))=="q");
 touch(w.x,w.y,true);touch(w.x,w.y,false);assert(std::string(lv_textarea_get_text(wifi_ctx.pass_input))=="qw");
 // Mode switching keeps release-to-type and no-repeat behavior.
 auto shift=key_center(kb,29);touch(shift.x,shift.y,true);touch(shift.x,shift.y,false);assert(lv_keyboard_get_mode(kb)==LV_KEYBOARD_MODE_TEXT_UPPER);
 q=key_center(kb,0);touch(q.x,q.y,true);touch(q.x,q.y,false);assert(std::string(lv_textarea_get_text(wifi_ctx.pass_input))=="qwQ");
 lv_keyboard_set_mode(kb,LV_KEYBOARD_MODE_TEXT_LOWER);lv_obj_remove_flag(wifi_ctx.content,LV_OBJ_FLAG_HIDDEN);lv_obj_send_event(wifi_ctx.pass_input,LV_EVENT_FOCUSED,nullptr);tick+=5100;capture("settings-keyboard");
 lv_textarea_set_text(wifi_ctx.ssid_input,"Test network");assert(save_wifi_form());assert(cfg.load("wifi")&&cfg.get("ssid","")=="Test network"&&cfg.get("password","")=="qwQ");leave_form();settle();
 // Save failure is reported and the form is retained.
 open_wifi_screen(nullptr);screen=lv_screen_active();lv_textarea_set_text(wifi_ctx.ssid_input,"Changed network");
 std::string path=std::string(getenv("GADGET_OS_DATA_DIR"))+"/config/wifi.json";std::string backup=path+".bak";assert(std::rename(path.c_str(),backup.c_str())==0);assert(mkdir(path.c_str(),0700)==0);
 assert(!save_wifi_form());assert(lv_screen_active()==screen&&form_dirty());assert(find_label(screen,"Could not save settings"));assert(std::remove(path.c_str())==0);assert(std::rename(backup.c_str(),path.c_str())==0);
 leave_form();settle();ui_load_screen(settings,true);settle();ui_load_screen(home,true);settle();
 // German forms and the shared keyboard also render at physical resolution.
 assert(i18n::set_language("de"));i18n::apply_fonts(display);settings_launch();settings=lv_screen_active();route_restore_settings_screen=settings;
 open_wifi_screen(nullptr);screen=lv_screen_active();lv_textarea_set_text(wifi_ctx.ssid_input,"Unsaved network");assert(confirm_form_back());capture("settings-unsaved-de");click(settings_form.dialog,"Discard");settle();
 open_wifi_screen(nullptr);lv_obj_send_event(wifi_ctx.pass_input,LV_EVENT_FOCUSED,nullptr);tick+=500;capture("settings-keyboard-de");leave_form();settle();ui_load_screen(home,true);settle();
 std::puts("Settings autosave, form validation/save/discard, failed storage and keyboard pointer tests passed");
}
