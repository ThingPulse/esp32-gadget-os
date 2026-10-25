#include "ui/I18n.h"
#include "apps/TransitApp.h"
#include "AppRegistry.h"
#include "services/TransitService.h"
#include "ui/UiNav.h"
#include "ui/UiSessionState.h"
#include "ui/StatusIcons.h"
#include <algorithm>
#include <ctime>
APP_REGISTER_COLOR_RANK("Transit",LV_SYMBOL_SHUFFLE,transit_app_launch,0x26b99a,25);
namespace {
constexpr uint32_t Bg=0x182131,Card=0x253449,Mint=0x55edc5,Muted=0xaab8d2,Amber=0xffc66d;
struct Context {
 lv_obj_t *screen=nullptr,*home=nullptr,*body=nullptr,*title=nullptr,*loading=nullptr,*input=nullptr;
 lv_timer_t *timer=nullptr;int view=0;uint32_t revision=0,tick=0;size_t remove=0;
 bool submitted=false;std::string query;transit::Departure departure;
}ctx;
void render();
lv_obj_t *box(lv_obj_t*p,int x,int y,int w,int h,uint32_t color=Card){auto*o=lv_obj_create(p);lv_obj_set_pos(o,x,y);lv_obj_set_size(o,w,h);lv_obj_set_style_bg_color(o,lv_color_hex(color),0);lv_obj_set_style_pad_all(o,0,0);lv_obj_set_style_border_width(o,0,0);lv_obj_set_style_radius(o,9,0);lv_obj_clear_flag(o,LV_OBJ_FLAG_SCROLLABLE);return o;}
lv_obj_t *label(lv_obj_t*p,const std::string&t,int x,int y,int w,uint32_t color=0xffffff,bool big=false){auto*l=lv_label_create(p);lv_label_set_text(l,t.c_str());lv_obj_set_pos(l,x,y);lv_obj_set_width(l,w);lv_label_set_long_mode(l,LV_LABEL_LONG_DOT);lv_obj_set_style_text_color(l,lv_color_hex(color),0);if(big)lv_obj_set_style_text_font(l,i18n::font24(),0);return l;}
lv_obj_t *button(lv_obj_t*p,const std::string&t,int x,int y,int w,int h,lv_event_cb_t cb,intptr_t data=0,uint32_t color=Card){auto*b=lv_button_create(p);lv_obj_set_pos(b,x,y);lv_obj_set_size(b,w,h);lv_obj_set_style_bg_color(b,lv_color_hex(color),0);lv_obj_set_style_shadow_width(b,0,0);lv_obj_add_event_cb(b,cb,LV_EVENT_CLICKED,reinterpret_cast<void*>(data));auto*l=lv_label_create(b);lv_label_set_text(l,t.c_str());lv_obj_set_width(l,w-16);lv_label_set_long_mode(l,LV_LABEL_LONG_DOT);lv_obj_set_style_text_align(l,LV_TEXT_ALIGN_CENTER,0);lv_obj_center(l);return b;}
// Explicit text bounds keep long destinations from overlapping metadata.
lv_obj_t *bounded_label(lv_obj_t *parent,const std::string &text,int x,int y,int width,int height,uint32_t color=0xffffff,bool big=false){
 auto *value=label(parent,text,x,y,width,color,big);lv_obj_set_height(value,height);return value;
}
void route_badge(lv_obj_t *parent,const transit::Departure &departure,int x,int y){
 auto *pill=box(parent,x,y,52,28,departure.color);
 auto *name=bounded_label(pill,departure.line,0,0,48,lv_font_get_line_height(lv_obj_get_style_text_font(pill,LV_PART_MAIN)));
 lv_obj_set_style_text_align(name,LV_TEXT_ALIGN_CENTER,0);lv_obj_center(name);
}
void board(){ctx.view=0;TransitService::instance().resume();render();}
void back(lv_event_t*){if(ctx.view==2||ctx.view==5){ctx.view=1;render();return;}if(ctx.view){board();return;}TransitService::instance().pause();UiSessionState::set_route("home");if(ctx.home&&lv_obj_is_valid(ctx.home))ui_load_screen(ctx.home,true);}
void stops(lv_event_t*){TransitService::instance().hold();ctx.view=1;render();}
void info(lv_event_t*){TransitService::instance().hold();ctx.view=4;render();}
void refresh(lv_event_t*){TransitService::instance().refresh();render();}
void navigate(lv_event_t*e){auto&s=TransitService::instance();size_t n=s.favourites().size();if(!n)return;int delta=int(reinterpret_cast<intptr_t>(lv_event_get_user_data(e)));s.select((s.selected()+n+delta)%n);render();}
void choose(lv_event_t*e){auto&s=TransitService::instance();s.select(size_t(reinterpret_cast<intptr_t>(lv_event_get_user_data(e))));ctx.view=0;render();}
void detail(lv_event_t*e){auto&s=TransitService::instance();size_t i=size_t(reinterpret_cast<intptr_t>(lv_event_get_user_data(e)));if(i>=s.board().departures.size())return;ctx.departure=s.board().departures[i];ctx.view=3;s.trip(ctx.departure.trip);render();}
void start_search(lv_event_t*){TransitService::instance().hold();ctx.view=2;ctx.submitted=false;render();}
void submit(lv_event_t*){ctx.query=lv_textarea_get_text(ctx.input);ctx.submitted=true;TransitService::instance().search(ctx.query);render();}
void add(lv_event_t*e){auto&s=TransitService::instance();size_t i=size_t(reinterpret_cast<intptr_t>(lv_event_get_user_data(e)));if(i<s.search_result().stops.size()&&s.add(s.search_result().stops[i]))ctx.view=0;render();}
void ask_remove(lv_event_t*e){ctx.remove=size_t(reinterpret_cast<intptr_t>(lv_event_get_user_data(e)));ctx.view=5;render();}
void remove(lv_event_t*){TransitService::instance().remove(ctx.remove);TransitService::instance().hold();ctx.view=1;render();}
std::string prediction(const transit::Departure&d){if(d.cancelled)return i18n::tr("Cancelled");if(!d.live)return i18n::tr("Scheduled");int delay=int((d.time-d.scheduled)/60);return delay>0?i18n::tr("Live  +")+std::to_string(delay)+i18n::tr(" min"):delay<0?i18n::tr("Live  ")+std::to_string(delay)+i18n::tr(" min"):i18n::tr("Live prediction");}
void render(){
 auto&s=TransitService::instance();ctx.revision=s.revision();int w=lv_obj_get_width(ctx.body),h=lv_obj_get_height(ctx.body);int scroll=lv_obj_get_scroll_y(ctx.body);
 lv_obj_clean(ctx.body);ctx.input=nullptr;lv_obj_scroll_to_y(ctx.body,0,LV_ANIM_OFF);
 lv_label_set_text(ctx.title,ctx.view==1?i18n::tr("Stops"):ctx.view==2?i18n::tr("Find a stop"):ctx.view==3?i18n::tr("Departure"):i18n::tr("Transit"));
 if(ctx.view==4){
  auto*l=label(ctx.body,i18n::tr("Transitous / MOTIS\n\nData and attribution:\nhttps://transitous.org/sources/\nOpenStreetMap contributors\n\nCoverage and live predictions vary by operator. All clock times use your device timezone.\n\nRefreshes every 2 minutes while open. Cached data is marked.\n\nPublic Transitous service: non-commercial use only. See transitous.org/api/"),12,12,w-24,Muted);lv_label_set_long_mode(l,LV_LABEL_LONG_WRAP);return;
 }
 if(ctx.view==5){label(ctx.body,i18n::tr("Remove favourite?"),12,20,w-24,0xffffff,true);if(ctx.remove<s.favourites().size())label(ctx.body,s.favourites()[ctx.remove].name,12,65,w-24);button(ctx.body,i18n::tr("Remove"),12,120,w-24,44,remove,0,0x9c4242);button(ctx.body,i18n::tr("Cancel"),12,178,w-24,44,back);return;}
 if(ctx.view==1){
  label(ctx.body,i18n::tr("Favourite stops"),12,10,w-24,Mint,true);int y=48;
  for(size_t i=0;i<s.favourites().size();++i){auto &f=s.favourites()[i];auto*r=box(ctx.body,12,y,w-24,64);lv_obj_add_flag(r,LV_OBJ_FLAG_CLICKABLE);lv_obj_add_event_cb(r,choose,LV_EVENT_CLICKED,reinterpret_cast<void*>(i));label(r,f.name,10,8,w-90);label(r,f.area,10,34,w-90,Muted);button(r,LV_SYMBOL_TRASH,w-75,10,40,42,ask_remove,i);y+=72;}
  if(s.favourites().empty()){label(ctx.body,i18n::tr("Save a stop to see departures"),12,y,w-24,Muted);y+=40;}
  button(ctx.body,i18n::tr(LV_SYMBOL_PLUS " Add stop"),12,y,w-24,44,start_search,0,0x237a70);y+=54;
  if(!s.notice().empty())label(ctx.body,s.notice(),12,y,w-24,Amber);return;
 }
 if(ctx.view==2){
  if(!ctx.submitted){
   label(ctx.body,i18n::tr("Station, city or stop name"),12,10,w-24,Muted);
   ctx.input=lv_textarea_create(ctx.body);lv_obj_set_pos(ctx.input,12,40);lv_obj_set_size(ctx.input,w-24,46);lv_textarea_set_one_line(ctx.input,true);lv_textarea_set_max_length(ctx.input,80);lv_textarea_set_text(ctx.input,ctx.query.c_str());
   lv_obj_set_style_bg_color(ctx.input,lv_color_hex(Card),0);lv_obj_set_style_text_color(ctx.input,lv_color_white(),0);lv_obj_set_style_border_color(ctx.input,lv_color_hex(Mint),LV_PART_CURSOR);lv_obj_set_style_border_width(ctx.input,2,LV_PART_CURSOR);lv_obj_set_style_border_side(ctx.input,LV_BORDER_SIDE_LEFT,LV_PART_CURSOR);lv_obj_add_state(ctx.input,LV_STATE_FOCUSED);
   button(ctx.body,i18n::tr("Search"),12,100,w-24,44,submit,0,0x237a70);
   auto*hint=label(ctx.body,i18n::tr("Tap Search to look up stops"),12,155,w-24,Muted);lv_label_set_long_mode(hint,LV_LABEL_LONG_WRAP);
   auto*k=lv_keyboard_create(ctx.body);lv_obj_set_align(k,LV_ALIGN_TOP_LEFT);lv_obj_set_pos(k,0,208);lv_obj_set_size(k,w,std::max(180,h-208));lv_keyboard_set_textarea(k,ctx.input);return;
  }
  label(ctx.body,ctx.query,12,8,w-24,Mint,true);button(ctx.body,i18n::tr("New search"),12,43,w-24,38,start_search);int y=92;
  auto&r=s.search_result();if(!r.error.empty()){label(ctx.body,r.error,12,y,w-24,Amber);y+=38;}
  if(!s.notice().empty()){label(ctx.body,s.notice(),12,y,w-24,Amber);y+=38;}
  if(r.stops.empty())label(ctx.body,s.busy()?(s.online()?i18n::tr("Searching..."):i18n::tr("Waiting for Wi-Fi / clock")):i18n::tr("No matching stops"),12,y,w-24,Muted);
  for(size_t i=0;i<r.stops.size();++i){auto*row=box(ctx.body,12,y,w-24,62);lv_obj_add_flag(row,LV_OBJ_FLAG_CLICKABLE);lv_obj_add_event_cb(row,add,LV_EVENT_CLICKED,reinterpret_cast<void*>(i));label(row,r.stops[i].name,10,7,w-52);label(row,r.stops[i].area+i18n::tr("  /  Tap to save"),10,33,w-52,Muted);y+=70;}return;
 }
 if(ctx.view==3){
  const auto&d=ctx.departure;auto*hero=box(ctx.body,12,10,w-24,148);
  route_badge(hero,d,12,12);
  bounded_label(hero,d.destination,76,12,w-112,40);
  bounded_label(hero,transit::countdown(d,std::time(nullptr)),12,56,w-48,30,d.cancelled?Amber:Mint,true);
  bounded_label(hero,transit::clock_text(d.time)+i18n::tr(" / Platform ")+(d.platform.empty()?"--":d.platform),12,94,w-48,20,Muted);
  bounded_label(hero,prediction(d),12,120,w-48,20,d.cancelled?Amber:d.live?Mint:Muted);int y=172;auto&r=s.details();
  if(!r.error.empty()){label(ctx.body,r.error,12,y,w-24,Amber);y+=30;}
  if(r.calls.empty()){label(ctx.body,s.busy()?i18n::tr("Loading route..."):i18n::tr("No route stops available"),12,y,w-24,Muted);y+=40;}
  // Hide already passed portions of the trip. Keep the boarding call even if
  // the feed represents it as a platform child with a different stop ID.
  size_t first=0;while(first<r.calls.size()&&r.calls[first].time&&r.calls[first].time<d.time-60)++first;
  for(size_t i=first;i<r.calls.size();++i){
   const auto&c=r.calls[i];auto*row=box(ctx.body,12,y,w-24,68,0x1d2c3e);
   lv_obj_set_style_radius(row,0,0);
   if(i+1<r.calls.size()){auto*rail=box(row,19,20,2,48,Muted);lv_obj_set_style_radius(rail,0,0);}
   auto*dot=box(row,14,14,12,12,i==first?Mint:0x1d2c3e);lv_obj_set_style_radius(dot,LV_RADIUS_CIRCLE,0);
   lv_obj_set_style_border_width(dot,2,0);lv_obj_set_style_border_color(dot,lv_color_hex(i==first?Mint:Muted),0);
   bounded_label(row,c.name,38,10,w-140,38,c.cancelled?Amber:0xffffff);
   auto*time=bounded_label(row,transit::clock_text(c.time),w-94,10,58,20,Mint);lv_obj_set_style_text_align(time,LV_TEXT_ALIGN_RIGHT,0);
   bounded_label(row,c.cancelled?i18n::tr("Cancelled"):c.platform.empty()?"":i18n::tr("Platform ")+c.platform,38,48,w-86,18,c.cancelled?Amber:Muted);y+=68;
  }
  y+=12;
  label(ctx.body,i18n::tr("Times: device timezone"),12,y,w-24,Muted);y+=28;button(ctx.body,i18n::tr("Back to departures"),12,y,w-24,42,back);lv_obj_scroll_to_y(ctx.body,scroll,LV_ANIM_OFF);return;
 }
 if(s.favourites().empty()){label(ctx.body,i18n::tr("Your next departure"),12,24,w-24,Mint,true);label(ctx.body,i18n::tr("Add a favourite stop to get started"),12,65,w-24,Muted);button(ctx.body,i18n::tr("Find a stop"),12,110,w-24,46,start_search,0,0x237a70);button(ctx.body,i18n::tr("About Transitous"),12,170,w-24,42,info);return;}
 const auto&stop=s.favourites()[s.selected()];
 auto*station=box(ctx.body,12,8,w-24,68,0x1d2c3e);
 lv_obj_add_flag(station,LV_OBJ_FLAG_CLICKABLE);lv_obj_add_event_cb(station,stops,LV_EVENT_CLICKED,nullptr);
 bounded_label(station,stop.name,12,10,w-84,24);
 bounded_label(station,LV_SYMBOL_LIST,w-60,12,24,22,Mint);
 const bool cached=!s.online()||!s.board().error.empty()||(s.fetched()&&TransitService::now_ms()-s.fetched()>180000);
 std::string status=!s.notice().empty()?s.notice():!s.online()?i18n::tr("Offline / waiting for clock"):!s.board().error.empty()?s.board().error:s.busy()?i18n::tr("Updating..."):s.fetched()?i18n::tr("Updated ")+std::to_string((TransitService::now_ms()-s.fetched())/1000)+i18n::tr("s ago"):i18n::tr("Waiting for departures");
 bounded_label(station,(cached&&s.fetched()?i18n::tr("Cached: "):"")+status,12,40,w-48,20,cached?Amber:Muted);
 int y=88;size_t shown=0;
 for(size_t i=0;i<s.board().departures.size();++i){
  const auto&d=s.board().departures[i];if(d.time<std::time(nullptr)-60)continue;
  bool hero=shown++==0;int height=hero?142:92;auto*row=box(ctx.body,12,y,w-24,height);
  lv_obj_add_flag(row,LV_OBJ_FLAG_CLICKABLE);lv_obj_add_event_cb(row,detail,LV_EVENT_CLICKED,reinterpret_cast<void*>(i));
  lv_obj_set_style_border_color(row,lv_color_hex(hero?Mint:0x34465b),0);lv_obj_set_style_border_width(row,1,0);
  route_badge(row,d,12,12);
  const std::string remaining=(cached?"~ ":"")+transit::countdown(d,std::time(nullptr));
  const uint32_t stateColor=d.cancelled?Amber:d.live?Mint:Muted;
  if(hero){
   bounded_label(row,d.destination,76,12,w-114,40);
   bounded_label(row,remaining,12,56,w-50,30,d.cancelled?Amber:Mint,true);
   bounded_label(row,transit::clock_text(d.time)+i18n::tr(" / Platform ")+(d.platform.empty()?"--":d.platform),12,92,w-50,20,Muted);
   bounded_label(row,prediction(d),12,116,w-50,20,stateColor);
  }else{
   const int countWidth=d.cancelled?92:68;
   bounded_label(row,d.destination,76,12,w-120-countWidth,40);
   auto*count=bounded_label(row,remaining,w-38-countWidth,12,countWidth,20,d.cancelled?Amber:0xffffff);
   lv_obj_set_style_text_align(count,LV_TEXT_ALIGN_RIGHT,0);
   bounded_label(row,transit::clock_text(d.time)+i18n::tr(" / Platform ")+(d.platform.empty()?"--":d.platform),76,52,w-114,18,Muted);
   bounded_label(row,prediction(d),76,72,w-114,18,stateColor);
  }
  y+=height+8;
 }
 if(!shown){label(ctx.body,s.busy()?i18n::tr("Loading departures..."):i18n::tr("No upcoming departures"),12,y,w-24,Muted);y+=42;}
 button(ctx.body,LV_SYMBOL_LEFT,12,y,44,40,navigate,-1);button(ctx.body,i18n::tr("Stops"),64,y,80,40,stops);button(ctx.body,LV_SYMBOL_REFRESH,152,y,68,40,refresh);button(ctx.body,LV_SYMBOL_RIGHT,w-56,y,44,40,navigate,1);y+=48;
 label(ctx.body,i18n::tr("Times: device timezone"),12,y,w-24,Muted);y+=26;button(ctx.body,i18n::tr("Transitous / Sources"),12,y,w-24,36,info);
 lv_obj_scroll_to_y(ctx.body,scroll,LV_ANIM_OFF);
}
void cleanup(lv_event_t*){if(ctx.timer)lv_timer_delete(ctx.timer);ctx.timer=nullptr;ctx.screen=nullptr;TransitService::instance().pause();}
}
void transit_app_launch(){
 ctx=Context{};ctx.home=lv_screen_active();ctx.screen=lv_obj_create(nullptr);lv_obj_set_style_bg_color(ctx.screen,lv_color_hex(Bg),0);lv_obj_set_style_pad_all(ctx.screen,0,0);lv_obj_set_style_border_width(ctx.screen,0,0);lv_obj_clear_flag(ctx.screen,LV_OBJ_FLAG_SCROLLABLE);
 int w=lv_display_get_horizontal_resolution(nullptr),h=lv_display_get_vertical_resolution(nullptr);auto*bar=box(ctx.screen,0,0,w,44,0x101426);lv_obj_set_style_radius(bar,0,0);button(bar,LV_SYMBOL_LEFT,4,0,44,44,back,0,0x101426);ctx.title=label(bar,i18n::tr("Transit"),65,12,w-125);lv_obj_set_style_text_align(ctx.title,LV_TEXT_ALIGN_CENTER,0);create_wifi_status_icon(bar,-8);
 ctx.loading=lv_spinner_create(bar);lv_obj_set_size(ctx.loading,18,18);lv_obj_set_pos(ctx.loading,w-55,12);lv_spinner_set_anim_params(ctx.loading,900,180);lv_obj_set_style_arc_width(ctx.loading,3,LV_PART_MAIN);lv_obj_set_style_arc_width(ctx.loading,3,LV_PART_INDICATOR);lv_obj_add_flag(ctx.loading,LV_OBJ_FLAG_HIDDEN);lv_obj_set_style_arc_color(ctx.loading,lv_color_hex(Mint),LV_PART_INDICATOR);
 ctx.body=lv_obj_create(ctx.screen);lv_obj_set_pos(ctx.body,0,44);lv_obj_set_size(ctx.body,w,h-44);lv_obj_set_style_pad_all(ctx.body,0,0);lv_obj_set_style_border_width(ctx.body,0,0);lv_obj_set_style_bg_opa(ctx.body,LV_OPA_TRANSP,0);lv_obj_set_scroll_dir(ctx.body,LV_DIR_VER);
 lv_obj_add_event_cb(ctx.body,[](lv_event_t*){if(ctx.view||!lv_indev_active())return;auto dir=lv_indev_get_gesture_dir(lv_indev_active());auto&s=TransitService::instance();auto n=s.favourites().size();if(n&&(dir==LV_DIR_LEFT||dir==LV_DIR_RIGHT)){s.select((s.selected()+n+(dir==LV_DIR_LEFT?1:-1))%n);render();}},LV_EVENT_GESTURE,nullptr);
 lv_obj_add_event_cb(ctx.screen,cleanup,LV_EVENT_DELETE,nullptr);ui_load_screen(ctx.screen,false);lv_obj_update_layout(ctx.screen);UiSessionState::set_route("transit");TransitService::instance().resume();render();
 ctx.timer=lv_timer_create([](lv_timer_t*){auto&s=TransitService::instance();if(s.busy())lv_obj_remove_flag(ctx.loading,LV_OBJ_FLAG_HIDDEN);else lv_obj_add_flag(ctx.loading,LV_OBJ_FLAG_HIDDEN);
  uint32_t tick=TransitService::now_ms()/10000;if(ctx.view==2&&!ctx.submitted)return;
  if(lv_obj_is_scrolling(ctx.body))return;
  auto*indev=lv_indev_get_next(nullptr);while(indev){if(lv_indev_get_state(indev)==LV_INDEV_STATE_PRESSED)return;indev=lv_indev_get_next(indev);}
  if(ctx.revision!=s.revision() || ((ctx.view==0||ctx.view==3)&&tick!=ctx.tick)){ctx.tick=tick;render();}},500,nullptr);
}
