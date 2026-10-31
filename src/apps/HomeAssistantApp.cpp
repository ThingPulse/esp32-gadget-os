#include "apps/HomeAssistantApp.h"
#include "services/HomeAssistantService.h"
#include "AppRegistry.h"
#include "ui/I18n.h"
#include "ui/UiNav.h"
#include "ui/UiSessionState.h"
#include "ui/StatusIcons.h"
#include <algorithm>
#include <tuple>
APP_REGISTER_EXPERIMENTAL_COLOR_RANK("Home",LV_SYMBOL_HOME,home_assistant_app_launch,0x26b99a,40);
namespace {
constexpr uint32_t Bg=0x182131,Card=0x253449,Mint=0x69efca,Muted=0xaab8d2;
struct TileWidgets{lv_obj_t*button=nullptr,*symbol=nullptr,*value=nullptr,*spinner=nullptr;};
struct Context{lv_obj_t*screen=nullptr,*home=nullptr,*body=nullptr,*title=nullptr,*spinner=nullptr;lv_timer_t*timer=nullptr;size_t page=0;int view=0;uint32_t revision=0;bool connected=false,detail_pending=false;std::string detail,detail_notice;uint32_t layout=0;ha::Entity detail_entity;lv_obj_t*status=nullptr,*notice=nullptr;TileWidgets tiles[ha::MaxTiles];}ctx;
void render();
void refresh();
lv_obj_t*label(lv_obj_t*p,const char*t,int x,int y,int w,uint32_t color=0xffffff,bool big=false){auto*l=lv_label_create(p);lv_label_set_text(l,t);lv_obj_set_pos(l,x,y);lv_obj_set_width(l,w);lv_label_set_long_mode(l,LV_LABEL_LONG_WRAP);lv_obj_set_style_text_color(l,lv_color_hex(color),0);lv_obj_set_style_text_font(l,big?i18n::font24():i18n::font14(),0);return l;}
lv_obj_t*label(lv_obj_t*p,const std::string&t,int x,int y,int w,uint32_t color=0xffffff,bool big=false){return label(p,t.c_str(),x,y,w,color,big);}
lv_obj_t*button(lv_obj_t*p,const char*t,int x,int y,int w,int h,lv_event_cb_t fn,intptr_t data=0,uint32_t color=Card){auto*b=lv_button_create(p);lv_obj_set_pos(b,x,y);lv_obj_set_size(b,w,h);lv_obj_set_style_bg_color(b,lv_color_hex(color),0);lv_obj_set_style_shadow_width(b,0,0);lv_obj_set_style_radius(b,10,0);lv_obj_set_style_pad_all(b,0,0);lv_obj_add_event_cb(b,fn,LV_EVENT_CLICKED,reinterpret_cast<void*>(data));if(*t){auto*l=label(b,t,0,0,w-12);lv_obj_set_style_text_align(l,LV_TEXT_ALIGN_CENTER,0);lv_obj_center(l);}return b;}
const ha::Tile*tile(){auto&pages=HomeAssistantService::instance().config().pages;for(auto&p:pages)for(auto&t:p.tiles)if(t.id==ctx.detail)return &t;return nullptr;}
void back(lv_event_t*){auto&s=HomeAssistantService::instance();if(ctx.view){s.close_setup();ctx.view=0;render();return;}s.pause();UiSessionState::set_route("home");if(ctx.home&&lv_obj_is_valid(ctx.home))ui_load_screen(ctx.home,true);}
void setup(lv_event_t*){if(HomeAssistantService::instance().open_setup())ctx.view=2;render();}
void navigate(lv_event_t*e){auto n=HomeAssistantService::instance().config().pages.size();if(n)ctx.page=(ctx.page+n+int(reinterpret_cast<intptr_t>(lv_event_get_user_data(e))))%n;render();}
void tap(lv_event_t*event){auto&s=HomeAssistantService::instance();auto&pages=s.config().pages;size_t i=reinterpret_cast<intptr_t>(lv_event_get_user_data(event));bool details=i>=ha::MaxTiles;i%=ha::MaxTiles;if(ctx.page>=pages.size()||i>=pages[ctx.page].tiles.size())return;ctx.detail=pages[ctx.page].tiles[i].id;auto d=ha::domain(ctx.detail);
 if(details||d=="sensor"||d=="binary_sensor"){ctx.view=1;render();return;}s.act(ctx.detail,d=="switch"||d=="light"?"toggle":"activate");refresh();}
void toggle(lv_event_t*){HomeAssistantService::instance().act(ctx.detail,"toggle");refresh();}
void color(lv_event_t*e){HomeAssistantService::instance().act(ctx.detail,"color",int(reinterpret_cast<intptr_t>(lv_event_get_user_data(e))));refresh();}
void slider(lv_event_t*e){auto*o=static_cast<lv_obj_t*>(lv_event_get_target(e));const char*action=static_cast<const char*>(lv_event_get_user_data(e));HomeAssistantService::instance().act(ctx.detail,action,lv_slider_get_value(o));}
const char*icon(const ha::Tile&t){auto kind=t.icon=="auto"||t.icon.empty()?ha::domain(t.id):t.icon;if(kind=="light")return LV_SYMBOL_CHARGE;if(kind=="switch"||kind=="power")return LV_SYMBOL_POWER;if(kind=="scene"||kind=="script"||kind=="play")return LV_SYMBOL_PLAY;if(kind=="water")return LV_SYMBOL_TINT;if(kind=="energy")return LV_SYMBOL_CHARGE;if(kind=="temperature")return "°";return LV_SYMBOL_EYE_OPEN;}
std::string value(const ha::Entity*e){if(!e)return i18n::tr("Unavailable");const char*expected=HomeAssistantService::instance().expected_state(e->id);if(expected)return i18n::tr(std::string(expected)=="on"?"On":"Off");if(!e->available())return i18n::tr("Unavailable");auto d=ha::domain(e->id);if(d=="scene"||d=="script")return i18n::tr("Activate");if(e->state=="on")return i18n::tr("On");if(e->state=="off")return i18n::tr("Off");return e->state+(e->unit.empty()?"":" "+e->unit);}
auto entity_fields(const ha::Entity&e){return std::tie(e.id,e.state,e.unit,e.brightness,e.kelvin,e.min_kelvin,e.max_kelvin,e.dimmable,e.warmth,e.color);}
void render(){
 auto&s=HomeAssistantService::instance();ctx.revision=s.revision();ctx.connected=s.connected();ctx.status=nullptr;ctx.notice=nullptr;for(auto&t:ctx.tiles)t={};ctx.layout=s.layout_revision();lv_obj_clean(ctx.body);lv_obj_scroll_to_y(ctx.body,0,LV_ANIM_OFF);if(ctx.view==1)lv_obj_add_flag(ctx.body,LV_OBJ_FLAG_SCROLLABLE);else lv_obj_remove_flag(ctx.body,LV_OBJ_FLAG_SCROLLABLE);int w=lv_obj_get_width(ctx.body);lv_label_set_text(ctx.title,i18n::tr(ctx.view==2?"Configure Home":ctx.view==1?"Details":"Home"));
 if(ctx.view==2){
  label(ctx.body,i18n::tr("Scan with your phone"),12,12,w-24,Mint,true);
  if(!s.setup_open()){label(ctx.body,i18n::tr("Setup expired. Open it again."),12,60,w-24);button(ctx.body,i18n::tr("Configure"),12,120,w-24,44,setup);return;}
  auto url=s.setup_url();
#if LV_USE_QRCODE
  auto*q=lv_qrcode_create(ctx.body);lv_qrcode_set_size(q,176);lv_qrcode_set_dark_color(q,lv_color_black());lv_qrcode_set_light_color(q,lv_color_white());lv_obj_set_pos(q,(w-176)/2,54);lv_qrcode_update(q,url.data(),url.size());
#endif
  label(ctx.body,url,12,242,w-24,Muted);label(ctx.body,i18n::tr("Same Wi-Fi. Setup closes after 10 minutes of inactivity. Keep this screen open while configuring."),12,304,w-24,Muted);button(ctx.body,i18n::tr("Done"),12,382,w-24,44,back);return;
 }
 if(ctx.view==1){
  ctx.detail_notice=s.notice();ctx.detail_pending=s.pending(ctx.detail);
  ctx.detail_entity=s.entity(ctx.detail)?*s.entity(ctx.detail):ha::Entity{};
  auto*t=tile();auto*e=s.entity(ctx.detail);if(!t){ctx.view=0;render();return;}
  auto*l=label(ctx.body,t->name.empty()?ctx.detail:t->name,12,14,w-24,0xffffff,true);lv_obj_update_layout(l);int titleBottom=14+lv_obj_get_height(l);
  label(ctx.body,value(e),12,titleBottom+10,w-24,ctx.connected?Mint:Muted,true);
  bool enabled=ctx.connected&&e&&e->available()&&!s.pending(ctx.detail);auto domain=ha::domain(ctx.detail);
  int footer=std::max(382,titleBottom+90);
  if(domain=="light"){
   auto*b=button(ctx.body,i18n::tr((s.expected_state(ctx.detail)?std::string(s.expected_state(ctx.detail))=="on":e&&e->state=="on")?"Turn off":"Turn on"),12,titleBottom+50,w-24,44,toggle);if(!enabled)lv_obj_add_state(b,LV_STATE_DISABLED);
   int y=titleBottom+112;
   auto add_slider=[&](const char*name,const char*action,int min,int max,int current,const std::string&v){label(ctx.body,i18n::tr(name),18,y,w-36);auto*l=label(ctx.body,v,w-100,y,82,Mint);lv_obj_set_style_text_align(l,LV_TEXT_ALIGN_RIGHT,0);auto*sl=lv_slider_create(ctx.body);lv_obj_set_pos(sl,28,y+38);lv_obj_set_size(sl,w-56,12);lv_slider_set_range(sl,min,max);lv_slider_set_value(sl,std::max(min,std::min(max,current)),LV_ANIM_OFF);lv_obj_set_style_bg_color(sl,lv_color_hex(Mint),LV_PART_INDICATOR);lv_obj_set_style_bg_color(sl,lv_color_white(),LV_PART_KNOB);lv_obj_add_event_cb(sl,slider,LV_EVENT_RELEASED,const_cast<char*>(action));if(!enabled)lv_obj_add_state(sl,LV_STATE_DISABLED);y+=80;};
   if(e&&e->dimmable)add_slider("Brightness","brightness",0,100,e->brightness<0?0:(e->brightness*100+127)/255,std::to_string(e->brightness<0?0:(e->brightness*100+127)/255)+"%");
   if(e&&e->warmth)add_slider("Warmth","warmth",e->min_kelvin,e->max_kelvin,e->kelvin,std::to_string(e->kelvin)+" K");
   if(e&&e->color){
    label(ctx.body,i18n::tr("Color"),18,y,w-36);y+=26;
    const uint32_t colors[]={0xff0000,0xff8000,0xffff00,0x00ff00,0x00ffff,0x0000ff,0x8000ff,0xff0080,0xffffff,0xffc080};
    int sw=(w-36-4*8)/5;
    for(int i=0;i<10;++i){auto*b=button(ctx.body,"",18+(i%5)*(sw+8),y+(i/5)*52,sw,44,color,colors[i],colors[i]);if(!enabled)lv_obj_add_state(b,LV_STATE_DISABLED);}
    y+=108;
   }
   footer=std::max(footer,y);
  }else label(ctx.body,ctx.detail,12,titleBottom+70,w-24,Muted);
  if(!s.notice().empty()){auto*n=label(ctx.body,i18n::tr(s.notice().c_str()),12,footer,w-24,0xffc66d);lv_obj_update_layout(n);footer+=lv_obj_get_height(n)+12;}
  button(ctx.body,i18n::tr("Done"),12,footer,w-24,44,back);return;
 }
 auto&pages=s.config().pages;
 if(pages.empty()||s.config().token.empty()){
  label(ctx.body,LV_SYMBOL_HOME,12,40,w-24,Mint,true);label(ctx.body,i18n::tr("Your home at a touch"),12,90,w-24,0xffffff,true);label(ctx.body,i18n::tr("Connect Home Assistant, choose your favourite entities and arrange them from your phone."),12,142,w-24,Muted);button(ctx.body,i18n::tr("Configure"),12,245,w-24,48,setup);if(!s.notice().empty())label(ctx.body,i18n::tr(s.notice().c_str()),12,308,w-24,0xffc66d);return;
 }
 ctx.page=std::min(ctx.page,pages.size()-1);auto&p=pages[ctx.page];auto*heading=label(ctx.body,p.name,12,8,w-72,0xffffff,true);lv_label_set_long_mode(heading,LV_LABEL_LONG_DOT);button(ctx.body,LV_SYMBOL_SETTINGS,w-54,2,44,44,setup);
 ctx.status=label(ctx.body,i18n::tr(ctx.connected?"Connected - refreshes every 5s":s.busy()?"Connecting...":"Disconnected"),12,47,w-24,ctx.connected?Mint:Muted);
 int tw=w-24;
 for(size_t i=0;i<p.tiles.size();++i){auto&t=p.tiles[i];auto*b=button(ctx.body,"",12,66+i*47,tw,44,tap,i);
  bool light=ha::domain(t.id)=="light";int content=tw-(light?44:0);
  if(light){auto*more=button(b,LV_SYMBOL_RIGHT,tw-44,0,44,44,tap,i+ha::MaxTiles);lv_obj_set_style_bg_opa(more,LV_OPA_TRANSP,0);lv_obj_remove_flag(more,LV_OBJ_FLAG_EVENT_BUBBLE);}
  auto*symbol=label(b,icon(t),10,14,18,Muted);lv_obj_clear_flag(symbol,LV_OBJ_FLAG_CLICKABLE);
  auto*name=label(b,t.name.empty()?t.id:t.name,36,4,content-42);lv_obj_set_style_text_align(name,LV_TEXT_ALIGN_LEFT,0);lv_obj_set_height(name,18);lv_label_set_long_mode(name,LV_LABEL_LONG_SCROLL_CIRCULAR);
  auto*v=label(b,"",36,24,content-62);lv_obj_set_style_text_align(v,LV_TEXT_ALIGN_LEFT,0);lv_label_set_long_mode(v,LV_LABEL_LONG_DOT);
  auto*pending=lv_spinner_create(b);lv_obj_set_size(pending,16,16);lv_obj_set_pos(pending,content-20,24);lv_spinner_set_anim_params(pending,900,180);lv_obj_set_style_arc_width(pending,2,LV_PART_MAIN);lv_obj_set_style_arc_width(pending,2,LV_PART_INDICATOR);lv_obj_remove_flag(pending,LV_OBJ_FLAG_CLICKABLE);lv_obj_add_flag(pending,LV_OBJ_FLAG_HIDDEN);
  ctx.tiles[i]={b,symbol,v,pending};
 }
 if(p.tiles.empty())label(ctx.body,i18n::tr("This page is empty. Add tiles in setup."),12,100,w-24,Muted);
 button(ctx.body,LV_SYMBOL_LEFT,12,354,44,40,navigate,-1);auto*pg=label(ctx.body,std::to_string(ctx.page+1)+" / "+std::to_string(pages.size()),66,366,w-132,Muted);lv_obj_set_style_text_align(pg,LV_TEXT_ALIGN_CENTER,0);button(ctx.body,LV_SYMBOL_RIGHT,w-56,354,44,40,navigate,1);
 ctx.notice=label(ctx.body,i18n::tr(s.notice().c_str()),12,403,w-24,0xffc66d);lv_label_set_long_mode(ctx.notice,LV_LABEL_LONG_DOT);
 refresh();
}
// Keep tile objects stable during polling: no disabled-state flash or lost focus.
void refresh(){
 auto&s=HomeAssistantService::instance();
 auto*detail=s.entity(ctx.detail);
 if(ctx.view==1&&ctx.layout==s.layout_revision()&&ctx.connected==s.connected()&&ctx.detail_notice==s.notice()&&ctx.detail_pending==s.pending(ctx.detail)&&((detail&&entity_fields(ctx.detail_entity)==entity_fields(*detail))||(!detail&&ctx.detail_entity.id.empty()))){ctx.revision=s.revision();ctx.connected=s.connected();return;}
 if(ctx.view!=0||!ctx.status||ctx.layout!=s.layout_revision()){int scroll=lv_obj_get_scroll_y(ctx.body);bool detailView=ctx.view==1;render();if(detailView)lv_obj_scroll_to_y(ctx.body,scroll,LV_ANIM_OFF);return;}
 ctx.revision=s.revision();ctx.connected=s.connected();
 auto text=[](lv_obj_t*l,const std::string&v){if(v!=lv_label_get_text(l))lv_label_set_text(l,v.c_str());};
 text(ctx.status,i18n::tr(ctx.connected?"Connected - refreshes every 5s":s.busy()?"Connecting...":"Disconnected"));
 lv_obj_set_style_text_color(ctx.status,lv_color_hex(ctx.connected?Mint:Muted),0);
 text(ctx.notice,i18n::tr(s.notice().c_str()));
 auto&p=s.config().pages[ctx.page];
 for(size_t i=0;i<p.tiles.size();++i){auto&t=p.tiles[i];auto*e=s.entity(t.id);auto&ui=ctx.tiles[i];
  const char*expected=s.expected_state(t.id);bool on=e&&(expected?std::string(expected)=="on":e->state=="on")&&ctx.connected;
  if(s.pending(t.id))lv_obj_remove_flag(ui.spinner,LV_OBJ_FLAG_HIDDEN);else lv_obj_add_flag(ui.spinner,LV_OBJ_FLAG_HIDDEN);
  if(lv_color_to_u32(lv_obj_get_style_bg_color(ui.button,LV_PART_MAIN))!=lv_color_to_u32(lv_color_hex(on?0x267968:Card)))lv_obj_set_style_bg_color(ui.button,lv_color_hex(on?0x267968:Card),0);
  if(lv_color_to_u32(lv_obj_get_style_text_color(ui.symbol,LV_PART_MAIN))!=lv_color_to_u32(lv_color_hex(on?Mint:Muted)))lv_obj_set_style_text_color(ui.symbol,lv_color_hex(on?Mint:Muted),0);
  if(lv_color_to_u32(lv_obj_get_style_text_color(ui.value,LV_PART_MAIN))!=lv_color_to_u32(lv_color_hex(on?Mint:0xffffff)))lv_obj_set_style_text_color(ui.value,lv_color_hex(on?Mint:0xffffff),0);text(ui.value,value(e));
  auto d=ha::domain(t.id);bool disabled=(!ctx.connected||!e||!e->available())&&d!="sensor"&&d!="binary_sensor";
  if(disabled)lv_obj_add_state(ui.button,LV_STATE_DISABLED);else lv_obj_remove_state(ui.button,LV_STATE_DISABLED);
 }
}
void cleanup(lv_event_t*){if(ctx.timer)lv_timer_delete(ctx.timer);ctx.timer=nullptr;ctx.screen=nullptr;HomeAssistantService::instance().pause();}
}
void home_assistant_app_launch(){
 ctx=Context{};ctx.home=lv_screen_active();ctx.screen=lv_obj_create(nullptr);lv_obj_set_style_bg_color(ctx.screen,lv_color_hex(Bg),0);lv_obj_set_style_text_font(ctx.screen,i18n::font14(),0);lv_obj_set_style_pad_all(ctx.screen,0,0);lv_obj_set_style_border_width(ctx.screen,0,0);lv_obj_clear_flag(ctx.screen,LV_OBJ_FLAG_SCROLLABLE);
 int w=lv_display_get_horizontal_resolution(nullptr),h=lv_display_get_vertical_resolution(nullptr);
 auto*bar=lv_obj_create(ctx.screen);lv_obj_set_pos(bar,0,0);lv_obj_set_size(bar,w,44);lv_obj_set_style_pad_all(bar,0,0);lv_obj_set_style_border_width(bar,0,0);lv_obj_set_style_bg_opa(bar,LV_OPA_TRANSP,0);lv_obj_clear_flag(bar,LV_OBJ_FLAG_SCROLLABLE);
 button(bar,LV_SYMBOL_LEFT,4,0,44,44,back);ctx.title=label(bar,i18n::tr("Home"),54,12,w-112);lv_obj_set_style_text_align(ctx.title,LV_TEXT_ALIGN_CENTER,0);create_wifi_status_icon(bar,-8);
 ctx.spinner=lv_spinner_create(bar);lv_obj_set_size(ctx.spinner,18,18);lv_obj_set_pos(ctx.spinner,w-56,12);lv_spinner_set_anim_params(ctx.spinner,900,180);lv_obj_set_style_arc_width(ctx.spinner,3,LV_PART_MAIN);lv_obj_set_style_arc_width(ctx.spinner,3,LV_PART_INDICATOR);lv_obj_add_flag(ctx.spinner,LV_OBJ_FLAG_HIDDEN);
 ctx.body=lv_obj_create(ctx.screen);lv_obj_set_pos(ctx.body,0,44);lv_obj_set_size(ctx.body,w,h-44);lv_obj_set_style_bg_opa(ctx.body,LV_OPA_TRANSP,0);lv_obj_set_style_border_width(ctx.body,0,0);lv_obj_set_style_pad_all(ctx.body,0,0);lv_obj_clear_flag(ctx.body,LV_OBJ_FLAG_SCROLLABLE);
 lv_obj_add_event_cb(ctx.body,[](lv_event_t*){if(ctx.view||!lv_indev_active())return;auto d=lv_indev_get_gesture_dir(lv_indev_active());auto n=HomeAssistantService::instance().config().pages.size();if(n&&(d==LV_DIR_LEFT||d==LV_DIR_RIGHT)){ctx.page=(ctx.page+n+(d==LV_DIR_LEFT?1:-1))%n;render();}},LV_EVENT_GESTURE,nullptr);
 lv_obj_add_event_cb(ctx.screen,cleanup,LV_EVENT_DELETE,nullptr);ui_load_screen(ctx.screen,false);lv_obj_update_layout(ctx.screen);UiSessionState::set_route("home_assistant");HomeAssistantService::instance().resume();render();
 ctx.timer=lv_timer_create([](lv_timer_t*){auto&s=HomeAssistantService::instance();if(s.busy())lv_obj_remove_flag(ctx.spinner,LV_OBJ_FLAG_HIDDEN);else lv_obj_add_flag(ctx.spinner,LV_OBJ_FLAG_HIDDEN);for(auto*i=lv_indev_get_next(nullptr);i;i=lv_indev_get_next(i))if(lv_indev_get_state(i)==LV_INDEV_STATE_PRESSED)return;if(ctx.revision!=s.revision()||ctx.connected!=s.connected())refresh();},250,nullptr);
}
