#pragma once
#include <lvgl.h>
#include <cstring>
#include <string>
namespace native_app::marketplace {
#include "MarketplacePreviews.inc"
struct Presentation {const char *id,*benefit,*setup,*symbol;uint32_t color;const lv_image_dsc_t *preview;bool computer;};
inline const Presentation *presentation(const std::string &id){
 static const Presentation apps[]={
  {"agent-usage","Follow coding agents and remaining usage.","Install Gadget Companion, pair your computer and enable Activity.",LV_SYMBOL_LIST,0x8053b3,&agent_usage_preview,true},
  {"system-monitor","See your Mac's CPU, memory and processes.","Install Gadget Companion on your Mac and pair it over Wi-Fi.",LV_SYMBOL_SETTINGS,0x287eaa,&system_monitor_preview,true},
  {"iss-tracker","Find the next visible space-station pass.","Set your device location. Wi-Fi refreshes pass predictions.",LV_SYMBOL_GPS,0x535eb3,&iss_tracker_preview,false},
  {"stocks","Keep prices and charts on your desk.","Connect to Wi-Fi and add stock or crypto symbols to your watchlist.",LV_SYMBOL_CHARGE,0x23846d,&stocks_preview,false},
  {"transit","Know when your next bus or train leaves.","Connect to Wi-Fi, search for your stop and save your favourites.",LV_SYMBOL_SHUFFLE,0x237e77,&transit_preview,false},
  {"planes","Identify the aircraft flying nearby.","Connect to Wi-Fi and choose your radar location in app settings.",LV_SYMBOL_GPS,0x287eaa,&planes_preview,false},
  {"home-assistant","Control your home from your desk.","Requires your Home Assistant server. Use your phone to configure the server, access token and controls.",LV_SYMBOL_HOME,0x237e77,&home_assistant_preview,false}
 };
 for(const auto &app:apps)if(id==app.id)return &app;return nullptr;
}
inline lv_obj_t *icon(lv_obj_t *parent,const Presentation *app,int size){
 auto *box=lv_obj_create(parent);lv_obj_set_size(box,size,size);lv_obj_set_style_radius(box,size/4,0);lv_obj_set_style_pad_all(box,0,0);lv_obj_set_style_border_width(box,0,0);lv_obj_set_style_bg_color(box,lv_color_hex(app?app->color:0x535e70),0);lv_obj_remove_flag(box,lv_obj_flag_t(LV_OBJ_FLAG_CLICKABLE|LV_OBJ_FLAG_SCROLLABLE));
 auto *label=lv_label_create(box);lv_label_set_text(label,app?app->symbol:LV_SYMBOL_DOWNLOAD);lv_obj_set_style_text_color(label,lv_color_white(),0);lv_obj_set_style_text_font(label,LV_FONT_DEFAULT,0);lv_obj_center(label);return box;
}
}
