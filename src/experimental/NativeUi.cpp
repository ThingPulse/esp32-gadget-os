#if GADGET_OS_NATIVE_APP_LOADER || !defined(ARDUINO)
#include "experimental/NativeUi.h"
#include "ui/I18n.h"
#include "ui/TouchKeyboard.h"
#include <cstring>
#include <vector>
#if GADGET_OS_NATIVE_APP_LOADER
#include "experimental/NativeApp.h"
#include <Arduino.h>
#endif
namespace native_app { namespace ui {
namespace {
struct Item {lv_obj_t *obj=nullptr,*caption=nullptr;uint32_t kind=0,depth=0;int selection=-1;gadget_callback callback=nullptr;void *user=nullptr;lv_chart_series_t *series=nullptr;uintptr_t token=0;std::vector<gadget_draw_command> drawing;bool drawing_hook=false;gadget_gesture_callback gesture=nullptr;void *gesture_user=nullptr;bool pull=false;uint32_t direction=0;bool gesture_hook=false;};
bool suspended=false;Item items[256];unsigned quota=64;uintptr_t next_token=1;lv_obj_t *screen=nullptr;void (*dispatch)(gadget_callback,void*)=nullptr;
Item *find(gadget_handle h){if(!screen || suspended)return nullptr;for(auto &i:items)if(h==reinterpret_cast<gadget_handle>(i.token) && i.obj)return &i;return nullptr;}
bool valid_text(const char *s){return s && strlen(s)<=160;}
void event(lv_event_t *e){
 auto *i=static_cast<Item*>(lv_event_get_user_data(e));auto code=lv_event_get_code(e);
#if GADGET_OS_NATIVE_APP_LOADER
 if(native_app::trace_enabled() && (code==LV_EVENT_PRESSED || code==LV_EVENT_RELEASED || code==LV_EVENT_CLICKED))
  Serial.printf("[NATIVE UI] event=%u button=%s\n",unsigned(code),i->caption?lv_label_get_text(i->caption):"");
#endif
 if(((i->kind==GADGET_UI_SLIDER&&code==LV_EVENT_RELEASED)||(i->kind!=GADGET_UI_SLIDER&&code==LV_EVENT_CLICKED)) && dispatch && find(reinterpret_cast<gadget_handle>(i->token)) && i->callback)dispatch(i->callback,i->user);
}
gadget_handle create(gadget_handle parent,uint32_t kind,const char *text,gadget_callback cb,void *user){
 if(!screen || suspended || kind>GADGET_UI_SLIDER || (text&&strlen(text)>(kind==GADGET_UI_TEXTAREA?255U:160U)))return nullptr;
 auto *p=find(parent);

 if(kind==GADGET_UI_ROOT){if(parent || items[0].obj)return nullptr;}
 else if(!p || (p->kind!=GADGET_UI_ROOT && p->kind!=GADGET_UI_CONTAINER && p->kind!=GADGET_UI_BUTTON) || p->depth>=8)return nullptr;
 Item *item=nullptr;for(unsigned n=0;n<quota;++n)if(!items[n].obj){item=&items[n];break;}if(!item)return nullptr;
 lv_obj_t *obj=kind==GADGET_UI_QRCODE?lv_qrcode_create(p->obj):kind==GADGET_UI_SLIDER?lv_slider_create(p->obj):kind==GADGET_UI_TEXTAREA?lv_textarea_create(p->obj):kind==GADGET_UI_KEYBOARD?lv_keyboard_create(p->obj):kind==GADGET_UI_ROOT?screen:kind==GADGET_UI_LABEL?lv_label_create(p->obj):kind==GADGET_UI_BUTTON?lv_button_create(p->obj):kind==GADGET_UI_CHART?lv_chart_create(p->obj):kind==GADGET_UI_SPINNER?lv_spinner_create(p->obj):kind==GADGET_UI_BAR?lv_bar_create(p->obj):lv_obj_create(p->obj);
 if(!obj)return nullptr;*item={obj,nullptr,kind,p?p->depth+1:0,-1,cb,user};item->token=next_token++;
 if(kind!=GADGET_UI_TEXTAREA&&kind!=GADGET_UI_KEYBOARD&&kind!=GADGET_UI_QRCODE&&kind!=GADGET_UI_SLIDER)lv_obj_remove_style_all(obj);
 if(kind!=GADGET_UI_TEXTAREA)lv_obj_clear_flag(obj,LV_OBJ_FLAG_SCROLLABLE);
 lv_obj_set_style_text_font(obj,i18n::font14(),0);lv_obj_set_style_text_color(obj,lv_color_hex(0xf1f3f5),0);
 if(kind==GADGET_UI_TEXTAREA){lv_textarea_set_max_length(obj,255);lv_textarea_set_text(obj,text?text:"");lv_obj_set_style_bg_color(obj,lv_color_hex(0x2a2d42),0);lv_obj_set_style_bg_opa(obj,LV_OPA_COVER,0);lv_obj_set_style_border_color(obj,lv_color_hex(0x63e6be),LV_PART_CURSOR);lv_obj_set_style_border_width(obj,2,LV_PART_CURSOR);lv_obj_set_style_border_side(obj,LV_BORDER_SIDE_LEFT,LV_PART_CURSOR);}
 if(kind==GADGET_UI_ROOT){lv_obj_set_layout(obj,LV_LAYOUT_NONE);lv_obj_set_style_bg_color(obj,lv_color_hex(0x172131),0);lv_obj_set_style_bg_opa(obj,LV_OPA_COVER,0);}
 if(kind==GADGET_UI_LABEL){lv_label_set_text(obj,text?text:"");lv_label_set_long_mode(obj,LV_LABEL_LONG_CLIP);}
 if(kind==GADGET_UI_BUTTON){item->caption=lv_label_create(obj);lv_label_set_text(item->caption,text?text:"");lv_obj_center(item->caption);lv_obj_set_style_bg_color(obj,lv_color_hex(0x213247),0);lv_obj_set_style_bg_opa(obj,LV_OPA_COVER,0);lv_obj_set_style_radius(obj,8,0);lv_obj_add_event_cb(obj,event,LV_EVENT_ALL,item);
 // remove_style_all also removes the theme's pressed feedback. Restore it for
 // every SDK button, including transparent header controls and selected tabs.
 lv_obj_set_style_bg_color(obj,lv_color_hex(0x38706f),LV_STATE_PRESSED);
 lv_obj_set_style_bg_opa(obj,LV_OPA_COVER,LV_STATE_PRESSED);
 lv_obj_set_style_text_color(obj,lv_color_white(),LV_STATE_PRESSED);
 lv_obj_set_style_outline_color(obj,lv_color_hex(0x8af2d5),LV_STATE_PRESSED);
 lv_obj_set_style_outline_width(obj,2,LV_STATE_PRESSED);
 lv_obj_set_style_outline_pad(obj,-2,LV_STATE_PRESSED);}
 if(kind==GADGET_UI_SLIDER)lv_obj_add_event_cb(obj,event,LV_EVENT_RELEASED,item);
 if(kind==GADGET_UI_BAR){lv_bar_set_range(obj,0,100);lv_obj_set_style_bg_opa(obj,LV_OPA_COVER,0);lv_obj_set_style_bg_color(obj,lv_color_hex(0x384f61),0);lv_obj_set_style_radius(obj,5,0);lv_obj_set_style_bg_opa(obj,LV_OPA_COVER,LV_PART_INDICATOR);lv_obj_set_style_bg_color(obj,lv_color_hex(0x63e6be),LV_PART_INDICATOR);lv_obj_set_style_radius(obj,5,LV_PART_INDICATOR);}
 if(kind==GADGET_UI_SPINNER){lv_spinner_set_anim_params(obj,1000,220);lv_obj_set_style_arc_width(obj,3,LV_PART_MAIN);lv_obj_set_style_arc_width(obj,3,LV_PART_INDICATOR);lv_obj_set_style_arc_color(obj,lv_color_hex(0x384f61),LV_PART_MAIN);lv_obj_set_style_arc_color(obj,lv_color_hex(0x63e6be),LV_PART_INDICATOR);lv_obj_clear_flag(obj,LV_OBJ_FLAG_CLICKABLE);}
 if(kind==GADGET_UI_CHART){lv_chart_set_type(obj,LV_CHART_TYPE_LINE);lv_chart_set_range(obj,LV_CHART_AXIS_PRIMARY_Y,0,100);lv_chart_set_point_count(obj,60);lv_chart_set_div_line_count(obj,0,0);lv_obj_set_style_size(obj,0,0,LV_PART_INDICATOR);lv_obj_set_style_line_width(obj,2,LV_PART_ITEMS);item->series=lv_chart_add_series(obj,lv_color_hex(0x63e6be),LV_CHART_AXIS_PRIMARY_Y);lv_chart_set_all_value(obj,item->series,LV_CHART_POINT_NONE);}
 return reinterpret_cast<gadget_handle>(item->token);
}
int32_t box(gadget_handle h,int32_t x,int32_t y,int32_t w,int32_t height){auto *i=find(h);if(!i || x<0 || y<0 || x>4096 || y>4096 || w<1 || height<1 || w>1024 || height>1024)return -1;lv_obj_set_align(i->obj,LV_ALIGN_TOP_LEFT);lv_obj_set_pos(i->obj,x,y);lv_obj_set_size(i->obj,w,height);return 0;}
int32_t flow(gadget_handle h,uint32_t axis,uint32_t pad,uint32_t gap){auto *i=find(h);if(!i || (i->kind!=GADGET_UI_ROOT&&i->kind!=GADGET_UI_CONTAINER) || axis>2 || pad>64 || gap>64)return -1;lv_obj_set_style_pad_all(i->obj,pad,0);lv_obj_set_style_pad_row(i->obj,gap,0);lv_obj_set_style_pad_column(i->obj,gap,0);if(axis)lv_obj_set_flex_flow(i->obj,axis==1?LV_FLEX_FLOW_ROW:LV_FLEX_FLOW_COLUMN);else lv_obj_set_layout(i->obj,LV_LAYOUT_NONE);return 0;}
int32_t style(gadget_handle h,uint32_t preset){auto *i=find(h);if(!i || preset>GADGET_STYLE_WHITE)return -1;auto *o=i->obj;
 switch(preset){
 case GADGET_STYLE_HEADER:case GADGET_STYLE_CARD:lv_obj_set_style_bg_opa(o,LV_OPA_COVER,0);lv_obj_set_style_bg_color(o,lv_color_hex(preset==GADGET_STYLE_HEADER?0x14162a:0x213247),0);lv_obj_set_style_radius(o,preset==GADGET_STYLE_HEADER?0:12,0);break;
 case GADGET_STYLE_WHITE:lv_obj_set_style_text_color(o,lv_color_hex(0xf1f3f5),0);break;
 case GADGET_STYLE_ELLIPSIS:if(i->kind!=GADGET_UI_LABEL)return -1;lv_label_set_long_mode(o,LV_LABEL_LONG_DOT);break;
 case GADGET_STYLE_TITLE:lv_obj_set_style_text_font(o,i18n::font24(),0);break;
 case GADGET_STYLE_VALUE:lv_obj_set_style_text_font(o,&lv_font_montserrat_32,0);lv_obj_set_style_text_color(o,lv_color_hex(0x63e6be),0);break;
 case GADGET_STYLE_MUTED:lv_obj_set_style_text_color(o,lv_color_hex(0xaebdcd),0);break;
 case GADGET_STYLE_ACCENT:lv_obj_set_style_text_color(o,lv_color_hex(0x63e6be),0);break;
 case GADGET_STYLE_BLUE:lv_obj_set_style_text_color(o,lv_color_hex(0x79d0fa),0);if(i->kind==GADGET_UI_BAR)lv_obj_set_style_bg_color(o,lv_color_hex(0x79d0fa),LV_PART_INDICATOR);break;
 case GADGET_STYLE_BACK:case GADGET_STYLE_REFRESH:if(!i->caption)return -1;lv_obj_set_style_bg_opa(o,LV_OPA_TRANSP,0);lv_label_set_text(i->caption,preset==GADGET_STYLE_BACK?LV_SYMBOL_LEFT:LV_SYMBOL_REFRESH);lv_obj_set_style_text_font(o,i18n::font24(),0);break;
 default:break;
 }return 0;
}
int32_t text(gadget_handle h,const char *s){auto *i=find(h);if(i&&i->kind==GADGET_UI_TEXTAREA){if(!s||strlen(s)>255)return -1;lv_textarea_set_text(i->obj,s);return 0;}if(!i || !valid_text(s) || (i->kind!=GADGET_UI_LABEL && !i->caption))return -1;auto *label=i->caption?i->caption:i->obj;if(!strcmp(lv_label_get_text(label),s))return 0;lv_label_set_text(label,s);if(i->caption)lv_obj_center(i->caption);return 0;}
int32_t value(gadget_handle h,int32_t v){auto *i=find(h);if(!i || i->kind!=GADGET_UI_BAR || v<0 || v>100)return -1;lv_bar_set_value(i->obj,v,LV_ANIM_OFF);return 0;}
int32_t visible(gadget_handle h,uint32_t v){auto *i=find(h);if(!i || v>1)return -1;if(bool(v)==!lv_obj_has_flag(i->obj,LV_OBJ_FLAG_HIDDEN))return 0;if(v)lv_obj_remove_flag(i->obj,LV_OBJ_FLAG_HIDDEN);else lv_obj_add_flag(i->obj,LV_OBJ_FLAG_HIDDEN);return 0;}
int32_t selected(gadget_handle h,uint32_t v){auto *i=find(h);if(!i || i->kind!=GADGET_UI_BUTTON || v>1)return -1;if(i->selection==int(v))return 0;i->selection=int(v);lv_obj_set_style_bg_color(i->obj,lv_color_hex(v?0x63e6be:0x213247),0);lv_obj_set_style_text_color(i->obj,lv_color_hex(v?0x172131:0xf1f3f5),0);return 0;}
int32_t align(gadget_handle h,uint32_t a){auto *i=find(h);if(!i || a>2 || (i->kind!=GADGET_UI_LABEL&&!i->caption))return -1;lv_obj_set_style_text_align(i->obj,a==0?LV_TEXT_ALIGN_LEFT:a==1?LV_TEXT_ALIGN_CENTER:LV_TEXT_ALIGN_RIGHT,0);return 0;}
int32_t chart(gadget_handle h,const int32_t *v,uint32_t n){auto *i=find(h);if(!i||i->kind!=GADGET_UI_CHART||!v||n>60)return -1;for(unsigned x=0;x<n;++x)if(v[x]<-1||v[x]>100)return -1;lv_chart_set_point_count(i->obj,60);int32_t *data=lv_chart_get_series_y_array(i->obj,i->series);bool changed=false;for(unsigned x=0;x<60;++x){int32_t value=x<60-n?-1:v[x-(60-n)];value=value<0?LV_CHART_POINT_NONE:value;if(data[x]!=value){data[x]=value;changed=true;}}if(changed)lv_chart_refresh(i->obj);return 0;}
int32_t opacity(gadget_handle h,uint32_t p){auto *i=find(h);if(!i||p>100)return -1;if(lv_obj_get_style_opa(i->obj,LV_PART_MAIN)!=p*255/100)lv_obj_set_style_opa(i->obj,p*255/100,0);return 0;}
int32_t reserve(uint32_t n){if(!screen||suspended||n<quota||n>256)return -1;quota=n;return 0;}
int32_t clear(gadget_handle h){auto*p=find(h);if(!p||(p->kind!=GADGET_UI_CONTAINER&&p->kind!=GADGET_UI_ROOT))return -1;
 for(auto&i:items)if(i.obj&&&i!=p){auto*a=lv_obj_get_parent(i.obj);while(a&&a!=p->obj)a=lv_obj_get_parent(a);if(a==p->obj)i={};}
 lv_obj_clean(p->obj);return 0;
}
int32_t scroll(gadget_handle h,uint32_t enable){auto*i=find(h);if(!i||i->kind!=GADGET_UI_CONTAINER||enable>1)return -1;if(enable){lv_obj_add_flag(i->obj,LV_OBJ_FLAG_SCROLLABLE);lv_obj_set_scroll_dir(i->obj,LV_DIR_VER);}else lv_obj_clear_flag(i->obj,LV_OBJ_FLAG_SCROLLABLE);return 0;}
int32_t paint(gadget_handle h,uint32_t fg,uint32_t bg,uint32_t opa){auto*i=find(h);if(!i||fg>0xffffff||bg>0xffffff||opa>100)return -1;lv_obj_set_style_text_color(i->obj,lv_color_hex(fg),0);lv_obj_set_style_bg_color(i->obj,lv_color_hex(bg),0);lv_obj_set_style_bg_opa(i->obj,opa*255/100,0);return 0;}
void drawing_event(lv_event_t*e){auto*i=static_cast<Item*>(lv_event_get_user_data(e));lv_area_t a;lv_obj_get_coords(i->obj,&a);auto*l=lv_event_get_layer(e);
 for(auto&c:i->drawing){if(c.kind==GADGET_DRAW_LINE){lv_draw_line_dsc_t d;lv_draw_line_dsc_init(&d);d.p1={lv_value_precise_t(a.x1+c.x),lv_value_precise_t(a.y1+c.y)};d.p2={lv_value_precise_t(a.x1+c.x2),lv_value_precise_t(a.y1+c.y2)};d.width=c.width;d.color=lv_color_hex(c.color);lv_draw_line(l,&d);}else{lv_draw_arc_dsc_t d;lv_draw_arc_dsc_init(&d);d.center={a.x1+c.x,a.y1+c.y};d.radius=c.x2;d.width=c.width;d.start_angle=0;d.end_angle=360;d.color=lv_color_hex(c.color);lv_draw_arc(l,&d);}}
}
int32_t draw(gadget_handle h,const gadget_draw_command*c,uint32_t n){auto*i=find(h);if(!i||i->kind!=GADGET_UI_CONTAINER||n>64||(!c&&n))return -1;unsigned total=n;for(auto&j:items)if(&j!=i)total+=j.drawing.size();if(total>128)return -1;
 for(unsigned k=0;k<n;++k){auto&v=c[k];if(v.kind<0||v.kind>1||v.x<0||v.y<0||v.x>1024||v.y>1024||v.x2<0||v.y2<0||v.x2>1024||v.y2>1024||v.width<1||v.width>16||v.color>0xffffff||(v.kind==1&&!v.x2))return -1;}
 if(!i->drawing_hook){lv_obj_add_event_cb(i->obj,drawing_event,LV_EVENT_DRAW_MAIN,i);i->drawing_hook=true;}i->drawing.clear();if(n)i->drawing.assign(c,c+n);lv_obj_invalidate(i->obj);return 0;
}
int32_t wrap(gadget_handle h){auto*i=find(h);if(!i||i->kind!=GADGET_UI_LABEL)return -1;lv_label_set_long_mode(i->obj,LV_LABEL_LONG_WRAP);return 0;}

int32_t input_config(gadget_handle h,uint32_t n,uint32_t multiline){auto*i=find(h);if(!i||i->kind!=GADGET_UI_TEXTAREA||!n||n>255||multiline>1)return -1;lv_textarea_set_max_length(i->obj,n);lv_textarea_set_one_line(i->obj,!multiline);return 0;}
int32_t input_get(gadget_handle h,char*out,uint32_t cap){auto*i=find(h);if(!i||i->kind!=GADGET_UI_TEXTAREA||!out||!cap||cap>1024)return -1;const char*s=lv_textarea_get_text(i->obj);size_t n=strlen(s);if(n>=cap)return -1;memcpy(out,s,n+1);return n;}
int32_t keyboard_attach(gadget_handle h,gadget_handle input){auto*k=find(h);auto*i=find(input);if(!k||!i||k->kind!=GADGET_UI_KEYBOARD||i->kind!=GADGET_UI_TEXTAREA)return -1;lv_keyboard_set_textarea(k->obj,i->obj);configure_touch_keyboard(k->obj,!lv_textarea_get_one_line(i->obj));return 0;}

int32_t scroll_y(gadget_handle h,int32_t y){auto*i=find(h);if(!i||i->kind!=GADGET_UI_CONTAINER||y < -1||y>4096)return -1;if(y>=0)lv_obj_scroll_to_y(i->obj,y,LV_ANIM_OFF);return lv_obj_get_scroll_y(i->obj);}

void gesture_event(lv_event_t *e){auto*i=static_cast<Item*>(lv_event_get_user_data(e));if(!find(reinterpret_cast<gadget_handle>(i->token))||!i->gesture||!dispatch)return;
 uint32_t direction=0;auto code=lv_event_get_code(e);
 if(code==LV_EVENT_SCROLL_BEGIN)i->pull=false;
 if(code==LV_EVENT_SCROLL&&lv_obj_get_scroll_y(i->obj)<=-56)i->pull=true;
 if(code==LV_EVENT_SCROLL_END&&i->pull){direction=GADGET_GESTURE_PULL;i->pull=false;}
 if(code==LV_EVENT_GESTURE&&lv_indev_active()){auto d=lv_indev_get_gesture_dir(lv_indev_active());if(d==LV_DIR_LEFT)direction=GADGET_GESTURE_LEFT;else if(d==LV_DIR_RIGHT)direction=GADGET_GESTURE_RIGHT;}
 if(direction){i->direction=direction;dispatch([](void*p){auto*i=static_cast<Item*>(p);if(i->gesture)i->gesture(i->direction,i->gesture_user);},i);}
}
int32_t gestures(gadget_handle h,gadget_gesture_callback cb,void*u){auto*i=find(h);if(!i||(i->kind!=GADGET_UI_CONTAINER&&i->kind!=GADGET_UI_ROOT))return -1;i->gesture=cb;i->gesture_user=u;if(!i->gesture_hook){lv_obj_add_event_cb(i->obj,gesture_event,LV_EVENT_ALL,i);i->gesture_hook=true;}return 0;}

int32_t chart_fit(gadget_handle h,const int32_t*v,uint32_t n){auto*i=find(h);if(!i||i->kind!=GADGET_UI_CHART||!v||!n||n>96)return -1;for(unsigned k=0;k<n;++k)if(v[k]<-1||v[k]>100)return -1;lv_chart_set_point_count(i->obj,n);auto*values=lv_chart_get_series_y_array(i->obj,i->series);for(unsigned k=0;k<n;++k)values[k]=v[k]<0?LV_CHART_POINT_NONE:v[k];lv_chart_refresh(i->obj);return 0;}

int32_t interaction_active(gadget_handle h){auto*i=find(h);if(h&&!i)return -1;if(i&&lv_obj_is_scrolling(i->obj))return 1;for(auto*input=lv_indev_get_next(nullptr);input;input=lv_indev_get_next(input))if(lv_indev_get_type(input)==LV_INDEV_TYPE_POINTER&&lv_indev_get_state(input)==LV_INDEV_STATE_PRESSED)return 1;return 0;}

}
int32_t qr(gadget_handle h,const char*t,uint32_t size){auto*i=find(h);if(!i||i->kind!=GADGET_UI_QRCODE||!t||strlen(t)>256||size<80||size>240)return -1;lv_qrcode_set_size(i->obj,size);lv_qrcode_set_dark_color(i->obj,lv_color_black());lv_qrcode_set_light_color(i->obj,lv_color_white());return lv_qrcode_update(i->obj,t,strlen(t))==LV_RESULT_OK?0:-1;}
int32_t slider(gadget_handle h,int32_t low,int32_t high,int32_t value){auto*i=find(h);if(!i||i->kind!=GADGET_UI_SLIDER||low<0||high>10000||low>=high||value<low||value>high)return -1;lv_slider_set_range(i->obj,low,high);lv_slider_set_value(i->obj,value,LV_ANIM_OFF);return 0;}
int32_t value_get(gadget_handle h){auto*i=find(h);return i&&i->kind==GADGET_UI_SLIDER?lv_slider_get_value(i->obj):-1;}
const gadget_ui_api api={sizeof(api),create,box,flow,style,text,value,visible,selected,align,chart,opacity,reserve,clear,scroll,paint,draw,wrap,input_config,input_get,keyboard_attach,scroll_y,gestures,chart_fit,interaction_active,qr,slider,value_get};
void begin(lv_obj_t *s,void (*d)(gadget_callback,void*)){end();quota=64;suspended=false;screen=s;dispatch=d;}
void suspend(){suspended=true;}
void end(){dispatch=nullptr; // Delete top-level owned widgets once; children follow.
 for(auto &i:items)if(i.obj && i.depth==1)lv_obj_delete(i.obj);
 for(auto &i:items)i={};screen=nullptr;
}
} }
#endif
