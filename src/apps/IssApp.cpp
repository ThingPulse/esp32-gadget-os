// Retained as the simulator parity reference and for firmware without the app loader.
#if !GADGET_OS_NATIVE_APP_LOADER && !GADGET_OS_APP_STORE_SIMULATOR
#include "apps/IssApp.h"
#include "apps/SettingsApp.h"
#include "AppRegistry.h"
#include "services/IssService.h"
#include "services/TimeService.h"
#include "services/WifiService.h"
#include "ui/I18n.h"
#include "ui/UiNav.h"
#include "ui/UiSessionState.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <ctime>

APP_REGISTER_ID_COLOR_RANK("iss", "ISS", LV_SYMBOL_GPS, iss_app_launch, 0x237e77, 27);
namespace {
constexpr uint32_t BG=0x1e2030,MINT=0x63e6be,MUTED=0x9aa5bd,WHITE=0xe7eaf4;
struct Context {
 lv_obj_t *screen=nullptr,*home=nullptr,*body=nullptr,*location=nullptr,*tabs[2]{},*refresh=nullptr,*status=nullptr;
 lv_timer_t *timer=nullptr;
 bool passes=false,visible_only=true,closing=false,was_active=true;
 int64_t selected=0,last_second=-1,list_expiry=0;
 unsigned revision=~0U;
 iss::Pass shown;
} ctx;
void render();
void plain(lv_obj_t *o){lv_obj_set_style_pad_all(o,0,0);lv_obj_set_style_border_width(o,0,0);lv_obj_set_style_radius(o,0,0);lv_obj_set_style_shadow_width(o,0,0);lv_obj_clear_flag(o,LV_OBJ_FLAG_SCROLLABLE);}
lv_obj_t *text(lv_obj_t *parent,int x,int y,int w,const std::string &s,uint32_t color=WHITE,const lv_font_t *font=nullptr,bool center=false){
 auto*o=lv_label_create(parent);lv_obj_set_pos(o,x,y);lv_obj_set_width(o,w);lv_label_set_text(o,s.c_str());
 lv_obj_set_style_text_color(o,lv_color_hex(color),0);lv_obj_set_style_text_font(o,font?font:i18n::font14(),0);
 if(center)lv_obj_set_style_text_align(o,LV_TEXT_ALIGN_CENTER,0);
 return o;
}
lv_obj_t *button(lv_obj_t *parent,int x,int y,int w,int h,const char *title,lv_event_cb_t callback,void *data=nullptr,bool active=false){
 auto*b=lv_button_create(parent);lv_obj_set_pos(b,x,y);lv_obj_set_size(b,w,h);plain(b);
 lv_obj_set_style_radius(b,8,0);lv_obj_set_style_bg_color(b,lv_color_hex(active?0x285c55:0x293549),0);
 auto*l=lv_label_create(b);lv_label_set_text(l,i18n::tr(title));lv_obj_set_style_text_color(l,lv_color_hex(active?MINT:WHITE),0);lv_obj_center(l);
 lv_obj_add_event_cb(b,callback,LV_EVENT_CLICKED,data);return b;
}
std::string clock_text(int64_t t){time_t epoch=t;tm local{};localtime_r(&epoch,&local);char b[24];strftime(b,sizeof(b),TimeService::instance().use_24h()?"%H:%M":"%I:%M %p",&local);return b;}
std::string date_text(int64_t t,int64_t now){time_t epoch=t,current=now;tm day{},today{};localtime_r(&epoch,&day);localtime_r(&current,&today);
 if(day.tm_year==today.tm_year&&day.tm_yday==today.tm_yday)return std::string(i18n::tr("Today"))+" - "+clock_text(t);
 char b[32];i18n::date(b,sizeof(b),day);return std::string(b)+" - "+clock_text(t);
}
std::string directions(const iss::Pass&p){return std::string(iss::compass(p.rise_azimuth))+" " LV_SYMBOL_RIGHT " "+iss::compass(p.set_azimuth);}
void stroke(lv_layer_t *layer,float x,float y,float xx,float yy,uint32_t color,int width=1){lv_draw_line_dsc_t d;lv_draw_line_dsc_init(&d);d.p1={lv_value_precise_t(x),lv_value_precise_t(y)};d.p2={lv_value_precise_t(xx),lv_value_precise_t(yy)};d.color=lv_color_hex(color);d.width=width;lv_draw_line(layer,&d);}
void dot(lv_layer_t*layer,int x,int y,int radius,uint32_t color){lv_draw_arc_dsc_t d;lv_draw_arc_dsc_init(&d);d.center={x,y};d.radius=radius;d.width=radius;d.start_angle=0;d.end_angle=360;d.color=lv_color_hex(color);lv_draw_arc(layer,&d);}
void compass_draw(lv_event_t *event){auto*layer=lv_event_get_layer(event);lv_area_t a;lv_obj_get_coords(lv_event_get_target_obj(event),&a);int x=a.x1+150,y=a.y1+70;
 lv_draw_arc_dsc_t d;lv_draw_arc_dsc_init(&d);d.center={x,y};d.radius=52;d.width=1;d.start_angle=0;d.end_angle=360;d.color=lv_color_hex(0x65748a);lv_draw_arc(layer,&d);
 for(double az:{ctx.shown.rise_azimuth,ctx.shown.set_azimuth}){double angle=az*3.141592653589793/180;dot(layer,x+std::lround(std::sin(angle)*52),y-std::lround(std::cos(angle)*52),5,MINT);}
}
void satellite_draw(lv_event_t*e){auto*layer=lv_event_get_layer(e);lv_area_t a;lv_obj_get_coords(lv_event_get_target_obj(e),&a);float x=a.x1+150,y=a.y1+20;
 stroke(layer,x-38,y,x+38,y,0x65748a,2);stroke(layer,x,y-13,x,y+13,MUTED,4);
 for(int side:{-1,1})for(int row:{-1,1}){float left=x+side*23-9,top=y+row*11-7;
  stroke(layer,left,top,left+18,top,0x65748a,2);stroke(layer,left,top+14,left+18,top+14,0x65748a,2);
  for(int n=0;n<=3;++n)stroke(layer,left+n*6,top,left+n*6,top+14,0x65748a,1);}
}
void open_location(lv_event_t*){IssService::instance().pause();ctx.was_active=false;settings_launch_with_route("settings_weather");}
void select_pass(lv_event_t *e){auto index=size_t(reinterpret_cast<uintptr_t>(lv_event_get_user_data(e)));const auto&passes=IssService::instance().forecast().passes;
 if(index<passes.size()){ctx.selected=passes[index].rise;ctx.passes=false;ctx.revision=~0U;render();}}
void filter(lv_event_t*e){ctx.visible_only=lv_event_get_user_data(e)==nullptr;ctx.revision=~0U;render();}
const iss::Pass *selected_pass(int64_t now){const auto&f=IssService::instance().forecast();
 for(const auto&p:f.passes)if(ctx.selected==p.rise&&p.set>now)return &p;
 ctx.selected=0;
 for(const auto&p:f.passes)if(p.visible&&p.rise<=now&&p.set>now)return &p;
 return iss::next(f,now,true);
}
std::string freshness(int64_t now){auto&s=IssService::instance();
 if(s.fetching())return i18n::tr("Updating predictions...");
 if(!s.error().empty())return i18n::tr(s.error().c_str());
 if(s.forecast().stale|| (s.fetched() && now-s.fetched()>21600))return i18n::tr("Old predictions - refresh needed");
#if defined(ARDUINO)
 if(!WifiService::instance().is_connected())return i18n::tr(s.fetched()?"Offline - cached predictions":"Connect to Wi-Fi for predictions");
 if(!TimeService::instance().is_synchronized())return i18n::tr("Waiting for clock sync");
#endif
 if(s.fetched())return std::string(i18n::tr("Updated"))+" "+iss::duration(now-s.fetched())+" "+i18n::tr("ago");
 return i18n::tr("Waiting for predictions");
}
void render(){if(!ctx.body||ctx.closing)return;auto&s=IssService::instance();int64_t now=time(nullptr);
 // The list is rebuilt only on a user action or new forecast, so scrolling stays stable.
 if(ctx.passes && ctx.revision==s.revision() && now<ctx.list_expiry){
  if(ctx.status)lv_label_set_text(ctx.status,freshness(now).c_str());return;
 }
 ctx.revision=s.revision();ctx.status=nullptr;lv_obj_clean(ctx.body);
 for(int i=0;i<2;++i){lv_obj_set_style_bg_color(ctx.tabs[i],lv_color_hex((i==1)==ctx.passes?0x285c55:BG),0);}
 lv_label_set_text(ctx.location,s.configured()?s.location().c_str():i18n::tr("Set your location"));
 if(!s.configured()){
  text(ctx.body,12,45,276,i18n::tr("Set your location"),WHITE,i18n::font24(),true);
  text(ctx.body,16,95,268,i18n::tr("ISS predictions use your Weather location, or your Planes location if Weather is not configured."),MUTED,nullptr,true);
  button(ctx.body,35,195,230,48,"Set location",open_location);return;
 }
 if(now<1600000000){text(ctx.body,15,95,270,i18n::tr("Waiting for clock sync"),WHITE,i18n::font24(),true);return;}
 if(ctx.passes){
  text(ctx.body,10,3,280,i18n::tr("Upcoming passes"),WHITE,i18n::font24(),true);
  button(ctx.body,10,36,136,44,"Visible",filter,nullptr,ctx.visible_only);
  button(ctx.body,154,36,136,44,"All",filter,reinterpret_cast<void*>(1),!ctx.visible_only);
  auto*list=lv_obj_create(ctx.body);lv_obj_set_pos(list,0,86);lv_obj_set_size(list,300,220);plain(list);lv_obj_set_style_bg_opa(list,LV_OPA_TRANSP,0);lv_obj_add_flag(list,LV_OBJ_FLAG_SCROLLABLE);lv_obj_set_scroll_dir(list,LV_DIR_VER);
  int row=0;size_t index=0;ctx.list_expiry=now+3600;
  for(const auto&p:s.forecast().passes){size_t current=index++;if(p.set<=now||(ctx.visible_only&&(!p.visible||p.visible_end<=now)))continue;
   ctx.list_expiry=std::min(ctx.list_expiry,ctx.visible_only?p.visible_end:p.set);
   auto*b=button(list,5,row++*76,290,68,"",select_pass,reinterpret_cast<void*>(current));
   text(b,10,7,270,date_text(ctx.visible_only?p.visible_start:p.rise,now));
   text(b,10,31,118,directions(p),MUTED);
   char peak[32];std::snprintf(peak,sizeof(peak),"%.0f°",p.elevation);text(b,132,31,45,peak,MINT);
   text(b,180,31,100,p.visible?iss::duration(p.visible_end-p.visible_start):i18n::tr("Not visible"),MUTED);
  }
  if(!row)text(list,12,40,276,i18n::tr(s.fetched()?"No passes in this forecast":"Waiting for predictions"),MUTED,nullptr,true);
  text(ctx.body,5,310,290,i18n::tr("Times shown in device local time"),MUTED,nullptr,true);
  ctx.status=text(ctx.body,5,331,290,freshness(now),MUTED,nullptr,true);lv_label_set_long_mode(ctx.status,LV_LABEL_LONG_DOT);return;
 }
 const auto*p=selected_pass(now);
 if(!p){text(ctx.body,8,40,284,i18n::tr(s.fetched()?"No visible passes forecast":"Looking for the ISS..."),WHITE,i18n::font14(),true);
  text(ctx.body,15,110,270,i18n::tr("Try All in the Passes tab. Visibility depends on sunlight and a dark sky."),MUTED,nullptr,true);
  text(ctx.body,5,300,290,freshness(now),MUTED,nullptr,true);return;}
 ctx.shown=*p;bool active=p->rise<=now;
 const lv_font_t*large=i18n::font24();
#if LV_FONT_MONTSERRAT_32
 large=&lv_font_montserrat_32;
#endif
 if(!active){
  text(ctx.body,0,4,300,i18n::tr(p->visible?"Next visible pass":"Next pass"),WHITE,i18n::german()?i18n::font14():i18n::font24(),true);
  text(ctx.body,0,36,300,std::string(i18n::tr("In"))+" "+iss::duration((p->visible?p->visible_start:p->rise)-now),MINT,large,true);
  text(ctx.body,0,76,300,date_text(p->visible?p->visible_start:p->rise,now),MUTED,nullptr,true);
  auto*compass=lv_obj_create(ctx.body);lv_obj_set_pos(compass,0,98);lv_obj_set_size(compass,300,140);plain(compass);lv_obj_set_style_bg_opa(compass,LV_OPA_TRANSP,0);lv_obj_add_event_cb(compass,compass_draw,LV_EVENT_DRAW_MAIN,nullptr);
  text(compass,140,0,20,"N",MUTED,nullptr,true);text(compass,140,123,20,"S",MUTED,nullptr,true);text(compass,73,61,18,"W",MUTED,nullptr,true);text(compass,211,61,18,"E",MUTED,nullptr,true);
  text(ctx.body,0,240,300,std::string(i18n::tr("Rise"))+" "+iss::compass(p->rise_azimuth)+"  " LV_SYMBOL_RIGHT "  "+i18n::tr("Set")+" "+iss::compass(p->set_azimuth),WHITE,nullptr,true);
  text(ctx.body,0,266,150,i18n::tr("Peak elevation"),MUTED,nullptr,true);text(ctx.body,150,266,150,i18n::tr(p->visible?"Visible for":"Duration"),MUTED,nullptr,true);
  char peak[24];std::snprintf(peak,sizeof(peak),"%.0f°",p->elevation);text(ctx.body,0,284,150,peak,WHITE,i18n::font24(),true);
  text(ctx.body,150,284,150,iss::duration(p->visible?p->visible_end-p->visible_start:p->set-p->rise),WHITE,i18n::font24(),true);
  text(ctx.body,0,315,300,i18n::tr("Visibility depends on clear skies"),MUTED,nullptr,true);
 }else{
  text(ctx.body,0,3,300,i18n::tr("Pass in progress"),MINT,i18n::font24(),true);
  text(ctx.body,0,35,300,std::string(i18n::tr("Rise"))+" "+iss::compass(p->rise_azimuth)+"  " LV_SYMBOL_RIGHT "  "+i18n::tr("Set")+" "+iss::compass(p->set_azimuth),WHITE,nullptr,true);
  bool before=p->visible&&now<p->visible_start;bool viewing=p->visible&&now>=p->visible_start&&now<p->visible_end;
  text(ctx.body,0,67,300,i18n::tr(before?"Visible window starts in":viewing?"Visible window ends in":"Pass ends in"),MUTED,nullptr,true);
  text(ctx.body,0,91,300,iss::duration((before?p->visible_start:viewing?p->visible_end:p->set)-now),MINT,large,true);
  auto*sat=lv_obj_create(ctx.body);lv_obj_set_pos(sat,0,137);lv_obj_set_size(sat,300,42);plain(sat);lv_obj_set_style_bg_opa(sat,LV_OPA_TRANSP,0);lv_obj_add_event_cb(sat,satellite_draw,LV_EVENT_DRAW_MAIN,nullptr);
  auto*bar=lv_bar_create(ctx.body);lv_obj_set_pos(bar,10,188);lv_obj_set_size(bar,280,12);lv_bar_set_range(bar,0,1000);lv_bar_set_value(bar,int(1000*(now-p->rise)/(p->set-p->rise)),LV_ANIM_OFF);lv_obj_set_style_bg_color(bar,lv_color_hex(MINT),LV_PART_INDICATOR);
  text(ctx.body,10,209,140,std::string(i18n::tr("Rise"))+" "+clock_text(p->rise),MUTED);text(ctx.body,165,209,130,std::string(i18n::tr("Set"))+" "+clock_text(p->set),MUTED);
  text(ctx.body,10,238,280,std::string(i18n::tr("Peak at"))+" "+clock_text(p->peak),WHITE,i18n::font24());
  char peak[100];std::snprintf(peak,sizeof(peak),i18n::tr("%.0f° above the horizon"),p->elevation);text(ctx.body,10,270,280,peak,MUTED);
  text(ctx.body,10,298,280,p->visible?std::string(i18n::tr("Visible until"))+" "+clock_text(p->visible_end):i18n::tr("Not visible - daylight or Earth shadow"),MUTED);
  text(ctx.body,10,315,280,i18n::tr("Predicted times; clear skies needed"),MUTED);
 }
 lv_label_set_long_mode(text(ctx.body,0,334,300,freshness(now),MUTED,nullptr,true),LV_LABEL_LONG_DOT);
}
void tick(lv_timer_t*){if(ctx.closing)return;bool active=lv_screen_active()==ctx.screen;
 if(active&&!ctx.was_active)IssService::instance().resume();ctx.was_active=active;if(!active)return;
 int64_t now=time(nullptr);if(now!=ctx.last_second||ctx.revision!=IssService::instance().revision()){ctx.last_second=now;render();}}
void tab(lv_event_t*e){ctx.passes=lv_event_get_user_data(e)!=nullptr;ctx.selected=0;ctx.revision=~0U;render();}
void close(lv_event_t*){if(ctx.closing)return;ctx.closing=true;IssService::instance().pause();ui_load_screen(ctx.home,true);}
void deleted(lv_event_t*){if(ctx.timer)lv_timer_delete(ctx.timer);IssService::instance().pause();ctx={};}
}
void iss_app_launch(){ctx={};ctx.home=lv_screen_active();ctx.screen=lv_obj_create(nullptr);plain(ctx.screen);lv_obj_set_style_bg_color(ctx.screen,lv_color_hex(BG),0);
 lv_obj_add_event_cb(ctx.screen,deleted,LV_EVENT_DELETE,nullptr);
 auto*header=lv_obj_create(ctx.screen);lv_obj_set_size(header,lv_pct(100),44);plain(header);lv_obj_set_style_bg_color(header,lv_color_hex(0x151b2a),0);
 button(header,0,0,44,44,LV_SYMBOL_LEFT,close);text(header,46,12,225,i18n::tr("Where is the ISS?"),WHITE,nullptr,true);
 ctx.refresh=button(header,276,0,44,44,LV_SYMBOL_REFRESH,[](lv_event_t*){IssService::instance().refresh();ctx.revision=~0U;render();});
 auto*location=button(ctx.screen,10,46,300,30,"",open_location);lv_obj_set_style_bg_opa(location,LV_OPA_TRANSP,0);lv_obj_set_ext_click_area(location,7);
 ctx.location=lv_obj_get_child(location,0);lv_obj_set_style_text_color(ctx.location,lv_color_hex(MUTED),0);
 ctx.body=lv_obj_create(ctx.screen);lv_obj_set_pos(ctx.body,10,78);lv_obj_set_size(ctx.body,300,354);plain(ctx.body);lv_obj_set_style_bg_opa(ctx.body,LV_OPA_TRANSP,0);
 ctx.tabs[0]=button(ctx.screen,0,432,160,48,"Next pass",tab,nullptr,true);ctx.tabs[1]=button(ctx.screen,160,432,160,48,"Passes",tab,reinterpret_cast<void*>(1));
 IssService::instance().resume();render();ctx.timer=lv_timer_create(tick,500,nullptr);ui_load_screen(ctx.screen,false);UiSessionState::set_route("iss");
}

#endif
