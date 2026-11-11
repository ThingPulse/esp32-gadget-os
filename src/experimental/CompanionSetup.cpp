#if GADGET_OS_NATIVE_APP_LOADER || GADGET_OS_APP_STORE_SIMULATOR
#include "experimental/CompanionSetup.h"
#include "experimental/NativeApp.h"
#include "experimental/Companion.h"
#include "ui/I18n.h"
#include <lvgl.h>
#include <cstring>

namespace native_app::companion_setup {
namespace {
constexpr const char *DownloadUrl="https://thingpulse.github.io/esp32-gadget-os/companion/";
bool requested=false,visible=false,closing=false,pair=false,waiting=false,preview=false;
std::string resume;
lv_obj_t *label(lv_obj_t *parent,const char *text,bool title=false){
    auto *value=lv_label_create(parent);lv_label_set_text(value,i18n::tr(text));lv_obj_set_width(value,lv_pct(100));
    lv_obj_set_style_text_font(value,title?i18n::font24():i18n::font14(),0);
    lv_obj_set_style_text_color(value,lv_color_hex(title?0x67e6c3:0xc8d5e2),0);return value;
}
void show(){
    requested=false;visible=true;native_launcher_release();auto *screen=lv_screen_active();
    lv_obj_set_style_bg_color(screen,lv_color_hex(0x172131),0);lv_obj_set_style_pad_all(screen,10,0);
    lv_obj_set_flex_flow(screen,LV_FLEX_FLOW_COLUMN);lv_obj_set_style_pad_row(screen,8,0);
    auto button=[&](const char *text,lv_event_cb_t action){auto *b=lv_button_create(screen);lv_obj_set_size(b,lv_pct(100),38);lv_obj_set_style_bg_color(b,lv_color_hex(0x237e77),0);auto *l=lv_label_create(b);lv_label_set_text(l,i18n::tr(text));lv_obj_center(l);lv_obj_add_event_cb(b,action,LV_EVENT_CLICKED,nullptr);};
    label(screen,"Connect computer",true);
    label(screen,"Install Gadget Companion on your Mac. Use the same Wi-Fi as your Gadget.");
    auto *qr=lv_qrcode_create(screen);lv_qrcode_set_size(qr,104);lv_qrcode_set_dark_color(qr,lv_color_black());lv_qrcode_set_light_color(qr,lv_color_white());lv_qrcode_update(qr,DownloadUrl,strlen(DownloadUrl));
    label(screen,"thingpulse.github.io/\nesp32-gadget-os/companion");
#if defined(ARDUINO)
    label(screen,"Open the app on your Mac, then tap Pair computer.");
    button("Pair computer",[](lv_event_t*){pair=true;closing=true;});
#else
    label(screen,"Previews use sample data. Pair from your physical Gadget.");
    if(!resume.empty())button("Open preview",[](lv_event_t*){preview=true;closing=true;});
#endif
    button("Back",[](lv_event_t*){closing=true;});
}
}
bool required(const std::string &id){return id=="agent-usage"||id=="system-monitor";}
void request(const char *app){if(!native_app::busy()&&native_launcher_is_active()){resume=app?app:"";requested=true;}}
bool active(){return requested||visible;}
void poll(){
    if(requested)show();
    if(closing){
        closing=false;visible=false;native_launcher_restore();
#if defined(ARDUINO)
        if(pair){pair=false;waiting=true;companion::request_pairing();}
        else resume.clear();
#else
        if(preview){preview=false;auto id=resume;resume.clear();native_app::launch_id(id.c_str());}else resume.clear();
#endif
    }
#if defined(ARDUINO)
    if(waiting&&!companion::active()&&native_launcher_is_active()){
        waiting=false;auto id=resume;resume.clear();
        if(companion::paired()){if(!id.empty())native_app::launch_id(id.c_str());}
        else request(id.empty()?nullptr:id.c_str());
    }
#endif
}
}
#endif
