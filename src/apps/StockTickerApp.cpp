#include "apps/StockTickerApp.h"
#include "AppRegistry.h"
#include "services/StockService.h"
#include "ui/UiNav.h"
#include "ui/UiSessionState.h"
#include "ui/StatusIcons.h"
#include "ui/StockChart.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <ctime>

APP_REGISTER_COLOR_RANK("Stocks", LV_SYMBOL_CHARGE, stock_ticker_app_launch, 0x26b99a, 30);
namespace {
constexpr uint32_t Bg=0x182131, Card=0x292d44, Mint=0x55edc5, Red=0xff877e, Muted=0xaab8d2;
struct Context {
    lv_obj_t *screen=nullptr,*home=nullptr,*body=nullptr,*input=nullptr,*error=nullptr,*notice=nullptr,*loading_line=nullptr;
    lv_timer_t *timer=nullptr;
    int view=0; // 0 watchlist, 1 detail, 2 editor
    size_t selected=0;
    stocks::Period period=stocks::Period::FiveDays;
    uint32_t revision=0, last_minute=0, notice_until=0;
    bool scrolling=false, pull_armed=false;
} ctx;
void render();
lv_obj_t *box(lv_obj_t *parent,int x,int y,int w,int h,uint32_t color=Card) {
    auto *o=lv_obj_create(parent); lv_obj_set_pos(o,x,y); lv_obj_set_size(o,w,h);
    lv_obj_set_style_bg_color(o,lv_color_hex(color),0);lv_obj_set_style_border_width(o,0,0);
    lv_obj_set_style_pad_all(o,0,0);lv_obj_set_style_radius(o,10,0);lv_obj_clear_flag(o,LV_OBJ_FLAG_SCROLLABLE);return o;
}
lv_obj_t *label(lv_obj_t *p,const std::string &text,int x,int y,int w,uint32_t color=0xffffff,bool large=false) {
    auto *o=lv_label_create(p);lv_label_set_text(o,text.c_str());lv_obj_set_pos(o,x,y);lv_obj_set_width(o,w);
    lv_label_set_long_mode(o,LV_LABEL_LONG_DOT);lv_obj_set_style_text_color(o,lv_color_hex(color),0);
    if(large)lv_obj_set_style_text_font(o,&lv_font_montserrat_24,0);return o;
}
lv_obj_t *button(lv_obj_t *p,const char *text,int x,int y,int w,int h,lv_event_cb_t fn,intptr_t data=0,uint32_t color=0x2196f3) {
    auto *o=lv_button_create(p);lv_obj_set_pos(o,x,y);lv_obj_set_size(o,w,h);
    lv_obj_set_style_bg_color(o,lv_color_hex(color),0);lv_obj_set_style_shadow_width(o,0,0);
    lv_obj_add_event_cb(o,fn,LV_EVENT_CLICKED,reinterpret_cast<void *>(data));
    auto *l=lv_label_create(o);lv_label_set_text(l,text);lv_obj_center(l);return o;
}
uint32_t change_color(const stocks::Quote &q) {
    return !q.has_previous_close?Muted:q.price>q.previous_close?Mint:q.price<q.previous_close?Red:Muted;
}
std::string quote_time(const stocks::Quote &q) {
    time_t t=static_cast<time_t>(q.timestamp); struct tm tm{};
#if defined(_WIN32)
    gmtime_s(&tm,&t);
#else
    gmtime_r(&t,&tm);
#endif
    char b[40];std::strftime(b,sizeof(b),"%d %b %H:%M UTC",&tm);return b;
}
std::string status(const StockService::Entry &e) {
    if(!e.error.empty()) return (e.valid?"Cached: ":"")+e.error;
    if(!e.valid) return "Waiting for quote / Wi-Fi";
    if(StockService::now_ms()-e.fetched>15*60000U) return "Cached - refresh pending";
    if(e.quote.delay_minutes>0) return std::to_string(e.quote.delay_minutes)+" min delayed";
    return "Last trade - may be delayed";
}
void update_loading() {
    const bool busy=StockService::instance().busy();
    const bool hidden=lv_obj_has_flag(ctx.loading_line,LV_OBJ_FLAG_HIDDEN);
    if(busy && hidden) {
        lv_obj_remove_flag(ctx.loading_line,LV_OBJ_FLAG_HIDDEN);
        lv_anim_t animation;
        lv_anim_init(&animation);
        lv_anim_set_var(&animation,ctx.loading_line);
        lv_anim_set_exec_cb(&animation,[](void *obj,int32_t x) {lv_obj_set_x(static_cast<lv_obj_t *>(obj),x);});
        lv_anim_set_values(&animation,-76,lv_display_get_horizontal_resolution(nullptr));
        lv_anim_set_duration(&animation,900);
        lv_anim_set_repeat_count(&animation,LV_ANIM_REPEAT_INFINITE);
        lv_anim_start(&animation);
    } else if(!busy && !hidden) {
        lv_anim_delete(ctx.loading_line,nullptr);
        lv_obj_add_flag(ctx.loading_line,LV_OBJ_FLAG_HIDDEN);
    }
}
void show_notice(const char *text) {
    lv_label_set_text(ctx.notice,text);
    lv_obj_remove_flag(ctx.notice,LV_OBJ_FLAG_HIDDEN);
    ctx.notice_until=StockService::now_ms()+4000;
}
void refresh(lv_event_t *) {
    auto &s=StockService::instance();
    const auto result=ctx.view==0?s.refresh_all():s.refresh(ctx.selected);
    show_notice(result==StockService::Refresh::Queued?"Refresh queued":
                result==StockService::Refresh::Recent?"Up to date (30s cooldown)":"Retry scheduled - please wait");
}
void pull_event(lv_event_t *event) {
    if(ctx.view!=0) return;
    const auto code=lv_event_get_code(event);
    if(code==LV_EVENT_SCROLL_BEGIN) ctx.scrolling=true;
    if(code==LV_EVENT_SCROLL) {
        const int pull=std::max<int32_t>(0,-lv_obj_get_scroll_y(ctx.body));
        if(pull>=56) ctx.pull_armed=true;
        if(pull>=8) show_notice(ctx.pull_armed?"Release to refresh":"Pull to refresh");
    }
    if(code==LV_EVENT_SCROLL_END) {
        const bool armed=ctx.pull_armed;
        ctx.pull_armed=false;ctx.scrolling=false;
        lv_obj_add_flag(ctx.notice,LV_OBJ_FLAG_HIDDEN);
        if(armed) refresh(nullptr);
    }
}
void row_click(lv_event_t *e) {
    ctx.selected=size_t(reinterpret_cast<intptr_t>(lv_event_get_user_data(e)));ctx.view=1;
    StockService::instance().select(ctx.selected,ctx.period);render();
}
void edit(lv_event_t *) {ctx.view=2;lv_obj_add_flag(ctx.notice,LV_OBJ_FLAG_HIDDEN);render();}
void save(lv_event_t *) {
    std::string error;
    if(!StockService::instance().save_watchlist(lv_textarea_get_text(ctx.input),error)) {lv_label_set_text(ctx.error,error.c_str());return;}
    ctx.selected=0;ctx.view=0;render();
}
void back(lv_event_t *) {
    if(ctx.view) {ctx.view=0;render();return;}
    UiSessionState::set_route("home"); StockService::instance().pause();
    if(ctx.home && lv_obj_is_valid(ctx.home)) ui_load_screen(ctx.home,true);
}
void navigate(lv_event_t *e) {
    auto &s=StockService::instance(); if(!s.count())return;
    int delta=int(reinterpret_cast<intptr_t>(lv_event_get_user_data(e)));
    ctx.selected=(ctx.selected+s.count()+delta)%s.count();
    s.select(ctx.selected,ctx.period);render();
}
void period_click(lv_event_t *e) {
    ctx.period=static_cast<stocks::Period>(reinterpret_cast<intptr_t>(lv_event_get_user_data(e)));
    StockService::instance().select(ctx.selected,ctx.period);render();
}
void render() {
    auto &s=StockService::instance(); const int w=lv_obj_get_width(ctx.body);
    const int h=lv_obj_get_height(ctx.body);
    const int scroll=lv_obj_get_scroll_y(ctx.body);
    lv_obj_clean(ctx.body);ctx.input=nullptr;ctx.error=nullptr;
    ctx.revision=s.revision();
    if(ctx.view==2) {
        label(ctx.body,"One symbol per line (maximum 10)",12,8,w-24);
        label(ctx.body,"Reorder lines to reorder your watchlist",12,29,w-24,Muted);
        ctx.input=lv_textarea_create(ctx.body);lv_obj_set_pos(ctx.input,12,54);lv_obj_set_size(ctx.input,w-24,110);
        lv_textarea_set_max_length(ctx.input,250);lv_textarea_set_text(ctx.input,s.watchlist_text().c_str());
        lv_obj_set_style_bg_color(ctx.input,lv_color_hex(Card),0);lv_obj_set_style_text_color(ctx.input,lv_color_white(),0);
        // Keep the caret visible against the dark editor, including while the
        // on-screen keyboard owns pointer focus.
        for(const lv_style_selector_t state : {lv_style_selector_t(LV_PART_CURSOR),
                                               lv_style_selector_t(LV_PART_CURSOR | LV_STATE_FOCUSED)}) {
            lv_obj_set_style_border_color(ctx.input,lv_color_hex(Mint),state);
            lv_obj_set_style_border_width(ctx.input,2,state);
            lv_obj_set_style_border_side(ctx.input,LV_BORDER_SIDE_LEFT,state);
            lv_obj_set_style_border_opa(ctx.input,LV_OPA_COVER,state);
            lv_obj_set_style_anim_duration(ctx.input,400,state);
        }
        lv_obj_add_state(ctx.input,LV_STATE_FOCUSED);
        ctx.error=label(ctx.body,"Examples: AAPL  NESN.SW  7203.T",12,170,w-24,Muted);
        button(ctx.body,"Save",12,198,(w-36)/2,38,save);
        button(ctx.body,"Cancel",24+(w-36)/2,198,(w-36)/2,38,back,0,Card);
        auto *keyboard=lv_keyboard_create(ctx.body);lv_obj_align(keyboard,LV_ALIGN_TOP_LEFT,0,244);lv_obj_set_size(keyboard,w,std::max(150,h-244));
        lv_keyboard_set_textarea(keyboard,ctx.input);return;
    }
    if(ctx.view==0) {
        label(ctx.body,"Watchlist",12,10,140,0xffffff,true);
        bool pending=false;
        for(size_t i=0;i<s.count();++i) pending=pending || s.entry(i).refresh_pending;
        label(ctx.body,s.busy()?"Refreshing...":pending?"Refresh queued":"Pull to refresh",w-140,17,128,Muted);
        int y=46;
        for(size_t i=0;i<s.count();++i) {
            const auto &e=s.entry(i);auto *row=box(ctx.body,12,y,w-24,55);
            lv_obj_add_flag(row,LV_OBJ_FLAG_CLICKABLE);lv_obj_add_event_cb(row,row_click,LV_EVENT_CLICKED,reinterpret_cast<void *>(i));
            label(row,e.symbol,10,7,105);
            auto *price=label(row,e.valid?stocks::price_text(e.quote.price,e.quote.currency):"--",110,7,w-144);
            lv_obj_set_style_text_align(price,LV_TEXT_ALIGN_RIGHT,0);
            label(row,e.valid?e.quote.name:(!e.error.empty()?e.error:"Waiting for quote"),10,32,140,Muted);
            auto *change=label(row,e.valid?stocks::change_text(e.quote,true):"",150,32,w-184,e.valid?change_color(e.quote):Muted);
            lv_obj_set_style_text_align(change,LV_TEXT_ALIGN_RIGHT,0);
            if(e.valid && (!e.error.empty() || StockService::now_ms()-e.fetched>900000U)) label(row,"Cached",10,32,140,0xffc66d);
            y+=61;
        }
        if(!s.count()) {label(ctx.body,"Add symbols to start your watchlist",12,y,w-24,Muted);y+=40;}
        label(ctx.body,std::string(s.provider_name())+" - quotes may be delayed",12,y,w-24,Muted);y+=26;
        button(ctx.body,"Edit watchlist",12,y,w-24,42,edit);
        // LVGL needs a scrollable extent to recognize elastic pulls on short lists.
        auto *extent=lv_obj_create(ctx.body);
        lv_obj_remove_style_all(extent);lv_obj_set_pos(extent,0,std::max(h,y+42));
        lv_obj_set_size(extent,1,1);lv_obj_clear_flag(extent,LV_OBJ_FLAG_CLICKABLE);lv_obj_clear_flag(extent,LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_scroll_to_y(ctx.body,scroll,LV_ANIM_OFF);return;
    }
    if(ctx.selected>=s.count()) {ctx.view=0;render();return;}
    const auto &e=s.entry(ctx.selected); const auto &q=e.quote;
    button(ctx.body,LV_SYMBOL_LEFT,12,10,45,40,navigate,-1);
    button(ctx.body,LV_SYMBOL_RIGHT,w-57,10,45,40,navigate,1);
    auto *symbol=label(ctx.body,e.symbol,60,13,w-120,Mint,true);lv_obj_set_style_text_align(symbol,LV_TEXT_ALIGN_CENTER,0);
    auto *name=label(ctx.body,e.valid?q.name+" / "+q.exchange:"Waiting for data",12,58,w-24,Muted);
    lv_obj_set_style_text_align(name,LV_TEXT_ALIGN_CENTER,0);
    label(ctx.body,e.valid?stocks::price_text(q.price,q.currency):"--",12,84,w-24,0xffffff,true);
    label(ctx.body,e.valid?stocks::change_text(q):"",12,119,w-24,e.valid?change_color(q):Muted);
    label(ctx.body,status(e),12,145,w-126,0xffc66d);
    auto *refresh_button=button(ctx.body,s.fetching(ctx.selected)?"Loading":e.refresh_pending?"Queued":LV_SYMBOL_REFRESH " Refresh",w-110,136,98,32,refresh,0,Card);
    if(e.refresh_pending || s.fetching(ctx.selected)) lv_obj_add_state(refresh_button,LV_STATE_DISABLED);
    auto *card=box(ctx.body,12,174,w-24,130);
    if(e.valid && q.period==ctx.period && q.count>1) {
        double lo=1e100,hi=-1e100;for(size_t i=0;i<q.count;++i)if(std::isfinite(q.samples[i].price)){lo=std::min(lo,q.samples[i].price);hi=std::max(hi,q.samples[i].price);}
        if(lo<=hi) {
            auto *chart=lv_chart_create(card);lv_obj_set_pos(chart,8,8);lv_obj_set_size(chart,w-40,114);
            lv_obj_set_style_bg_opa(chart,LV_OPA_TRANSP,0);lv_obj_set_style_border_width(chart,0,0);
            lv_obj_set_style_pad_all(chart,6,0);
            lv_obj_set_style_line_color(chart,lv_color_hex(0x454e65),LV_PART_MAIN);
            const size_t steps=std::max(size_t(1),std::min(size_t(12),size_t(std::max(1,w-53))/(q.count-1)));
            const size_t points=(q.count-1)*steps+1;
            lv_chart_set_type(chart,LV_CHART_TYPE_LINE);lv_chart_set_point_count(chart,points);
            lv_obj_set_style_line_width(chart,2,LV_PART_ITEMS);
            lv_obj_set_style_line_rounded(chart,true,LV_PART_ITEMS);
            lv_obj_add_flag(chart,LV_OBJ_FLAG_SEND_DRAW_TASK_EVENTS);
            lv_obj_add_event_cb(chart,stock_chart::shade,LV_EVENT_DRAW_TASK_ADDED,nullptr);
            lv_chart_set_range(chart,LV_CHART_AXIS_PRIMARY_Y,0,1000);lv_chart_set_div_line_count(chart,3,0);
            lv_obj_set_style_size(chart,0,0,LV_PART_INDICATOR);
            auto *series=lv_chart_add_series(chart,lv_color_hex(Mint),LV_CHART_AXIS_PRIMARY_Y);
            for(size_t i=0;i<points;++i) {
                const double value=stock_chart::sample(q,i/steps,double(i%steps)/steps);
                lv_chart_set_value_by_id(chart,series,i,!std::isfinite(value)?LV_CHART_POINT_NONE:
                    hi==lo?500:int(80+840*(value-lo)/(hi-lo)));
            }
        } else label(card,"No history available",12,50,w-48,Muted);
    } else label(card,e.error.empty()?"Loading daily history...":"History unavailable",12,50,w-48,Muted);
    for(int i=0;i<3;++i) {
        const auto period=static_cast<stocks::Period>(i);
        auto *control=button(ctx.body,stocks::period_text(period),12+i*(w-24)/3,314,(w-36)/3,36,period_click,i,
                              ctx.period==period?0x2196f3:Card);
        if(!s.supports(period)) lv_obj_add_state(control,LV_STATE_DISABLED);
    }
    label(ctx.body,e.valid?"Quote: "+quote_time(q):"Quote time unavailable",12,362,w-24,Muted);
    label(ctx.body,e.valid && q.has_previous_close?"Prev close: "+stocks::price_text(q.previous_close,q.currency):"Previous close unavailable",12,385,w-24,Muted);
    label(ctx.body,std::string(s.provider_name())+" / daily closes",12,408,w-24,Muted);
}
void cleanup(lv_event_t *) {
    if(ctx.timer)lv_timer_delete(ctx.timer);ctx.timer=nullptr;ctx.screen=nullptr;
    StockService::instance().pause();
}
}
void stock_ticker_app_launch() {
    ctx=Context{};ctx.home=lv_screen_active();ctx.screen=lv_obj_create(nullptr);
    lv_obj_set_style_bg_color(ctx.screen,lv_color_hex(Bg),0);lv_obj_set_style_pad_all(ctx.screen,0,0);
    lv_obj_set_style_border_width(ctx.screen,0,0);lv_obj_clear_flag(ctx.screen,LV_OBJ_FLAG_SCROLLABLE);
    auto *bar=box(ctx.screen,0,0,lv_display_get_horizontal_resolution(nullptr),44,0x101426);lv_obj_set_style_radius(bar,0,0);
    button(bar,LV_SYMBOL_LEFT,4,0,48,44,back,0,0x101426);
    auto *title=lv_label_create(bar);lv_label_set_text(title,"Stocks");lv_obj_set_style_text_color(title,lv_color_white(),0);lv_obj_center(title);
    create_wifi_status_icon(bar,-8);
    ctx.loading_line=box(bar,-76,41,76,3,0x64c8ff);
    lv_obj_set_style_radius(ctx.loading_line,LV_RADIUS_CIRCLE,0);
    lv_obj_clear_flag(ctx.loading_line,LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(ctx.loading_line,LV_OBJ_FLAG_HIDDEN);
    update_loading();
    ctx.body=lv_obj_create(ctx.screen);lv_obj_set_pos(ctx.body,0,44);
    lv_obj_set_size(ctx.body,lv_pct(100),lv_display_get_vertical_resolution(nullptr)-44);
    lv_obj_set_style_pad_all(ctx.body,0,0);lv_obj_set_style_border_width(ctx.body,0,0);
    lv_obj_set_style_bg_opa(ctx.body,LV_OPA_TRANSP,0);lv_obj_set_scroll_dir(ctx.body,LV_DIR_VER);
    lv_obj_add_flag(ctx.body,LV_OBJ_FLAG_SCROLL_ELASTIC);
    lv_obj_add_event_cb(ctx.body,pull_event,LV_EVENT_SCROLL_BEGIN,nullptr);
    lv_obj_add_event_cb(ctx.body,pull_event,LV_EVENT_SCROLL,nullptr);
    lv_obj_add_event_cb(ctx.body,pull_event,LV_EVENT_SCROLL_END,nullptr);
    ctx.notice=lv_label_create(ctx.screen);
    lv_obj_set_style_text_color(ctx.notice,lv_color_white(),0);
    lv_obj_set_style_bg_color(ctx.notice,lv_color_hex(Card),0);
    lv_obj_set_style_bg_opa(ctx.notice,LV_OPA_COVER,0);
    lv_obj_set_style_pad_all(ctx.notice,8,0);
    lv_obj_set_style_radius(ctx.notice,8,0);
    lv_obj_align(ctx.notice,LV_ALIGN_TOP_MID,0,50);
    lv_obj_add_flag(ctx.notice,LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(ctx.body,[](lv_event_t *) {
        if(ctx.view!=1 || !lv_indev_active()) return;
        const auto dir=lv_indev_get_gesture_dir(lv_indev_active());
        auto &s=StockService::instance();
        if(!s.count() || (dir!=LV_DIR_LEFT && dir!=LV_DIR_RIGHT)) return;
        ctx.selected=(ctx.selected+s.count()+(dir==LV_DIR_LEFT?1:-1))%s.count();
        s.select(ctx.selected,ctx.period);render();
    },LV_EVENT_GESTURE,nullptr);
    lv_obj_add_event_cb(ctx.screen,cleanup,LV_EVENT_DELETE,nullptr);
    ui_load_screen(ctx.screen,false);lv_obj_update_layout(ctx.screen);
    UiSessionState::set_route("stocks");StockService::instance().resume();render();
    ctx.timer=lv_timer_create([](lv_timer_t *) {
        update_loading();
        const uint32_t minute=StockService::now_ms()/60000;
        if(!ctx.scrolling && int32_t(StockService::now_ms()-ctx.notice_until)>=0) lv_obj_add_flag(ctx.notice,LV_OBJ_FLAG_HIDDEN);
        if(!ctx.scrolling && ctx.view!=2 && (ctx.revision!=StockService::instance().revision() || ctx.last_minute!=minute)) {ctx.last_minute=minute;render();}
    },500,nullptr);
}
