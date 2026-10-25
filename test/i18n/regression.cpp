#include <cassert>
#include <cstring>
#include <cstdio>
#include "ui/I18n.h"
#include "ui/AsciiFold.h"
#include "../../src/apps/SettingsApp.cpp"
struct Message {const char *en,*de;};
#include "../../src/ui/GermanMessages.inc"
static lv_obj_t *find(lv_obj_t *root,const lv_obj_class_t *type,const char *text=nullptr){
 if(lv_obj_check_type(root,type)&&(!text||!std::strcmp(lv_label_get_text(root),text)))return root;
 for(uint32_t i=0;i<lv_obj_get_child_count(root);++i)if(auto *found=find(lv_obj_get_child(root,i),type,text))return found;
 return nullptr;
}
int main(int argc,char**){
 i18n::init();
 if(argc>1){assert(i18n::german());std::puts("German preference survives process restart");return 0;}
 assert(ascii_fold_latin("München, Straße, Zürich")=="München, Straße, Zürich");
 assert(!i18n::german());assert(!std::strcmp(i18n::tr("Weather"),"Weather"));
 assert(!i18n::set_language("invalid"));assert(!i18n::german());
 assert(i18n::set_language("de"));assert(i18n::take_reload_request());assert(!i18n::take_reload_request());
 for(const auto &message:messages)assert(!std::strcmp(i18n::tr(message.en),message.de));
 assert(!std::strcmp(i18n::tr("Uncatalogued fallback"),"Uncatalogued fallback"));
 assert(!std::strcmp(i18n::weekday(0,true),"So"));assert(!std::strcmp(i18n::tr("Sun"),"Sonne"));
 tm day{};day.tm_year=126;day.tm_mon=9;day.tm_mday=24;day.tm_wday=6;char date[64];i18n::date(date,sizeof(date),day);
 assert(!std::strcmp(date,"Samstag, 24.10.2026"));
 assert(i18n::set_language("en"));i18n::take_reload_request();
 for(const auto &message:messages)assert(!std::strcmp(i18n::tr(message.en),message.en));
 lv_init();auto *display=lv_display_create(320,480);i18n::apply_fonts(display);
 for(auto *font:{i18n::font14(),i18n::font24()})for(uint32_t code:{0xC4,0xD6,0xDC,0xE4,0xF6,0xFC,0xDF,0x2013}){
  lv_font_glyph_dsc_t glyph{};assert(lv_font_get_glyph_dsc(font,&glyph,code,0));assert(glyph.adv_w>0);
 }
 auto *home=lv_screen_active();settings_launch();auto *settings=lv_screen_active();
 auto *choice=find(settings,&lv_dropdown_class);assert(choice);lv_dropdown_set_selected(choice,1);
 auto *caption=find(settings,&lv_label_class,"Apply language");assert(caption);
 lv_obj_send_event(lv_obj_get_parent(caption),LV_EVENT_CLICKED,nullptr);
 assert(i18n::german()&&i18n::take_reload_request());
 lv_screen_load(home);lv_obj_delete(settings);
 settings_launch();settings=lv_screen_active();choice=find(settings,&lv_dropdown_class);assert(lv_dropdown_get_selected(choice)==1);
 caption=find(settings,&lv_label_class,"Sprache übernehmen");assert(caption);
 lv_dropdown_set_selected(choice,0);lv_obj_send_event(lv_obj_get_parent(caption),LV_EVENT_CLICKED,nullptr);
 assert(!i18n::german()&&i18n::take_reload_request());
 lv_screen_load(home);lv_obj_delete(settings);
 assert(i18n::set_language("de"));
 std::printf("%zu translation entries, language switching, persistence and glyph coverage passed\n",sizeof(messages)/sizeof(messages[0]));
}
