#include "ui/I18n.h"
#include "services/ConfigStoreService.h"
#include <atomic>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <string>
extern "C" {
LV_FONT_DECLARE(german_glyphs_14);
LV_FONT_DECLARE(german_glyphs_24);
}
namespace i18n {
namespace {
std::atomic<bool> selected_german{false};
bool reload_requested=false;
struct Message { const char *en; const char *de; };
#include "GermanMessages.inc"
}
const char *tr(const char *english) {
 if (!english || !german()) return english;
 size_t first=0,last=sizeof(messages)/sizeof(messages[0]);
 while(first<last){size_t mid=first+(last-first)/2;int order=std::strcmp(english,messages[mid].en);
  if(!order)return messages[mid].de;if(order<0)last=mid;else first=mid+1;}
 return english;
}
bool german(){return selected_german.load(std::memory_order_relaxed);}
const char *language_code(){return german()?"de":"en";}
void init(){
 auto &cfg=ConfigStoreService::instance();
 if(cfg.begin()&&cfg.load("language"))selected_german.store(cfg.get("code","en")=="de");
#if !defined(ARDUINO)
 // Native documentation/test override; never persisted to user settings.
 if(const char *code=std::getenv("GADGET_OS_LANGUAGE"))selected_german.store(std::strcmp(code,"de")==0);
#endif
}
bool set_language(const char *code){
 if(!code || (std::strcmp(code,"en")&&std::strcmp(code,"de")))return false;
 auto &cfg=ConfigStoreService::instance();if(!cfg.begin()||!cfg.load("language"))return false;
 cfg.set("code",std::string(code));if(!cfg.save())return false;
 bool next=std::strcmp(code,"de")==0;
 if(next!=german()){selected_german.store(next);reload_requested=true;}
 return true;
}
bool take_reload_request(){bool result=reload_requested;reload_requested=false;return result;}
const lv_font_t *font14(){static lv_font_t font=lv_font_montserrat_14;font.fallback=&german_glyphs_14;return &font;}
const lv_font_t *font24(){static lv_font_t font=lv_font_montserrat_24;font.fallback=&german_glyphs_24;return &font;}
void apply_fonts(lv_display_t *display){
 auto *theme=lv_theme_default_init(display,lv_palette_main(LV_PALETTE_BLUE),lv_palette_main(LV_PALETTE_RED),LV_THEME_DEFAULT_DARK,font14());
 lv_display_set_theme(display,theme);
}
const char *weekday(int day,bool abbreviated){
 static const char *en[]={"Sunday","Monday","Tuesday","Wednesday","Thursday","Friday","Saturday"};
 static const char *short_en[]={"Sun","Mon","Tue","Wed","Thu","Fri","Sat"};
 static const char *de[]={"So","Mo","Di","Mi","Do","Fr","Sa"};
 if(day<0||day>6)return "";
 return abbreviated?(german()?de[day]:short_en[day]):tr(en[day]);
}
void date(char *buffer,size_t size,const tm &value){
 std::snprintf(buffer,size,"%s, %02d.%02d.%04d",weekday(value.tm_wday),value.tm_mday,value.tm_mon+1,value.tm_year+1900);
}
}
