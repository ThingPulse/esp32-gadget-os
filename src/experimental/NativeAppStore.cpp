#if GADGET_OS_NATIVE_APP_LOADER
#include "experimental/NativeAppStore.h"
#include "experimental/NativeCatalog.h"
#include "experimental/NativeStore.h"
#include "experimental/NativeApp.h"
#include "services/UpdateService.h"
#include "ui/I18n.h"
#include "NativeTrust.h"
#include <Arduino.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <WiFi.h>
#include <Preferences.h>
#include <esp_heap_caps.h>
#include <freertos/task.h>
#include <atomic>
#include <algorithm>
#include <new>
#include <set>

namespace native_app::app_store {
namespace {
struct Transfer {
    std::atomic<bool> done{false},cancelled{false};
    std::atomic<size_t> received{0};
    bool package=false;catalog::Item item;
    std::string url,error;size_t limit=65536,size=0;
    uint8_t *bytes=nullptr;
    ~Transfer(){heap_caps_free(bytes);}
};
Transfer *transfer=nullptr;
bool requested=false,visible=false,closing=false,loaded=false,has_new=false;
bool background_started=false,redraw=false,browser_requested=false;
uint32_t last_check=0;
int selection=-1;
size_t page=0;
constexpr size_t PageSize=4;
std::vector<catalog::Item> items;
std::vector<store::Installed> installed_apps;
std::set<std::string> new_ids;
std::string message,open_id;
lv_obj_t *content=nullptr,*status_label=nullptr,*progress=nullptr,*badge=nullptr,*refresh_button=nullptr;

bool ota_busy(){auto s=UpdateService::instance().state();return s==UpdateService::State::Downloading||s==UpdateService::State::ReadyToReboot;}
class Sink:public Stream {
    Transfer &t;uint32_t began=millis();
public:
    explicit Sink(Transfer &value):t(value){}
    size_t write(uint8_t b)override{return write(&b,1);}
    size_t write(const uint8_t *b,size_t n)override{
        if(t.cancelled.load()||millis()-began>30000){t.error="Download cancelled or timed out.";return 0;}
        if(n>t.limit-t.size){t.error="Download exceeds the size limit.";return 0;}
        memcpy(t.bytes+t.size,b,n);t.size+=n;t.received.store(t.size);return n;
    }
    int available()override{return 0;}int read()override{return -1;}int peek()override{return -1;}void flush()override{}
};
void fetch(Transfer &t){
    t.bytes=(uint8_t*)heap_caps_malloc(t.limit,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    if(!t.bytes){t.error="Not enough memory to download an app.";return;}
    WiFiClientSecure client;client.useBuiltinCACertBundle();client.setHandshakeTimeout(8);
    HTTPClient http;http.setConnectTimeout(5000);http.setTimeout(5000);
    http.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);
    if(!catalog::allowed_url(t.url,GADGET_OS_APP_CATALOG_URL)||!http.begin(client,t.url.c_str())){t.error="Invalid download address.";return;}
    if(t.cancelled.load())return;
    int status=http.GET();
    if(status!=200)t.error=status==404?"The app catalog is not published yet. Please try again later.":"Cannot reach the app store. Check Wi-Fi and try again.";
    else if(http.getSize()>int(t.limit))t.error="Download exceeds the size limit.";
    else {
        Sink sink(t);
        if(http.writeToStream(&sink)<0&&t.error.empty())t.error="Download interrupted. Please try again.";
        if(t.error.empty()&&http.getSize()>=0&&t.size!=size_t(http.getSize()))t.error="Incomplete download. Please try again.";
        if(t.error.empty()&&t.package&&t.size!=t.item.bytes)t.error="Package size does not match the catalog.";
    }
    http.end();
}
void worker(void *arg){
    auto *t=(Transfer*)arg;fetch(*t);
    t->done.store(true,std::memory_order_release);vTaskDelete(nullptr);
}
void update_badge(){if(badge){if(has_new)lv_obj_remove_flag(badge,LV_OBJ_FLAG_HIDDEN);else lv_obj_add_flag(badge,LV_OBJ_FLAG_HIDDEN);}}
void remember_catalog(bool mark_seen){
    Preferences prefs;std::string seen="|";
    installed_apps=store::installed();
    if(prefs.begin("app-store",false)){
        const size_t length=prefs.getBytesLength("seen-ids");
        if(length&&length<=4352){seen.resize(length);prefs.getBytes("seen-ids",seen.data(),length);}
        new_ids.clear();for(auto &i:items){
            const bool installed=std::any_of(installed_apps.begin(),installed_apps.end(),[&](const auto &a){return a.trusted&&a.manifest.id==i.id;});
            if(!installed&&seen.find("|"+i.id+"|")==std::string::npos)new_ids.insert(i.id);
        }
        if(mark_seen){std::string current="|";for(auto &i:items)current+=i.id+"|";prefs.putBytes("seen-ids",current.data(),current.size());}
        prefs.end();
    }
    has_new=!mark_seen&&!new_ids.empty();update_badge();
}
void start(bool package=false){
    if(transfer)return;
    if(WiFi.status()!=WL_CONNECTED){message="Connect to Wi-Fi, then tap Refresh.";redraw=true;return;}
    if(ota_busy()){message="Wait for the OS update to finish.";redraw=true;return;}
    auto *t=new(std::nothrow) Transfer;
    if(!t){message="Not enough memory. Please try again.";redraw=true;return;}
    t->package=package;
    if(package){
        if(selection<0||size_t(selection)>=items.size()){delete t;return;}
        t->item=items[selection];t->url=t->item.url;t->limit=t->item.bytes;
    }else t->url=GADGET_OS_APP_CATALOG_URL;
    transfer=t;last_check=millis();background_started=true;
    if(xTaskCreate(worker,"app-store",8192,t,1,nullptr)!=pdPASS){transfer=nullptr;delete t;message="Not enough memory. Please try again.";}
    else message=package?"Downloading app…":"Checking for apps…";
    redraw=true;
}
const store::Installed *find_installed(const std::string &id){
    for(auto &i:installed_apps)if(i.manifest.id==id)return &i;return nullptr;
}
lv_obj_t *text(lv_obj_t *parent,const char *value,bool small=false){
    auto *label=lv_label_create(parent);lv_obj_set_width(label,lv_pct(100));
    lv_label_set_text(label,i18n::tr(value));
    lv_obj_set_style_text_font(label,small?i18n::font14():i18n::font24(),0);
    lv_obj_set_style_text_color(label,lv_color_hex(small?0xb6c7d8:0xedf4fa),0);return label;
}
lv_obj_t *button(lv_obj_t *parent,const char *value,lv_event_cb_t cb,void *data=nullptr){
    auto *b=lv_button_create(parent);lv_obj_set_size(b,lv_pct(100),44);
    lv_obj_set_style_bg_color(b,lv_color_hex(0x237e77),0);
    auto *label=lv_label_create(b);lv_label_set_text(label,i18n::tr(value));lv_obj_center(label);
    lv_obj_add_event_cb(b,cb,LV_EVENT_CLICKED,data);return b;
}
void render(){
    redraw=false;if(!visible||!content)return;
    lv_obj_clean(content);progress=nullptr;status_label=nullptr;
    lv_obj_scroll_to_y(content,0,LV_ANIM_OFF);
    if(refresh_button){if(transfer)lv_obj_add_state(refresh_button,LV_STATE_DISABLED);else lv_obj_remove_state(refresh_button,LV_STATE_DISABLED);}
    if(!message.empty())status_label=text(content,message.c_str(),true);
    if(transfer){
        if(transfer->package){progress=lv_bar_create(content);lv_obj_set_size(progress,lv_pct(100),18);}
        text(content,"You can go back to cancel the download.",true);return;
    }
    if(selection>=0&&size_t(selection)<items.size()){
        const auto &item=items[selection];text(content,item.name.c_str());text(content,item.description.c_str(),true);
        text(content,("Version "+item.version).c_str(),true);
        const auto *old=find_installed(item.id);
        const bool same=old&&old->trusted&&old->manifest.version==item.version;
        button(content,same?"Open app":old?"Update app":"Install app",[](lv_event_t*){
            const auto &item=items[selection];const auto *old=find_installed(item.id);
            if(old&&old->trusted&&old->manifest.version==item.version){open_id=item.id;closing=true;}else start(true);
        });
        if(!same)text(content,"The app will appear on the Installed apps screen.",true);
        return;
    }
    if(loaded&&items.empty())text(content,"No compatible apps are available yet.");
    for(size_t index=page*PageSize;index<std::min(items.size(),(page+1)*PageSize);++index){
        auto &item=items[index];auto *card=lv_button_create(content);
        lv_obj_set_size(card,lv_pct(100),80);
        lv_obj_set_style_bg_color(card,lv_color_hex(0x213247),0);lv_obj_set_style_border_width(card,0,0);
        lv_obj_set_style_shadow_width(card,0,0);lv_obj_set_style_pad_all(card,10,0);
        auto *name=lv_label_create(card);lv_label_set_text(name,item.name.c_str());
        lv_obj_set_style_text_font(name,i18n::font24(),0);lv_obj_set_width(name,lv_pct(88));
        lv_obj_set_height(name,30);lv_label_set_long_mode(name,LV_LABEL_LONG_DOT);
        lv_obj_align(name,LV_ALIGN_TOP_LEFT,0,0);
        const auto *old=find_installed(item.id);const bool same=old&&old->trusted&&old->manifest.version==item.version;
        const char *state=same?"Installed":old?"Update available":new_ids.count(item.id)?"New":"View app";
        auto *detail=lv_label_create(card);lv_label_set_text(detail,i18n::tr(state));
        lv_obj_set_style_text_font(detail,i18n::font14(),0);lv_obj_set_style_text_color(detail,lv_color_hex(0x67e6c3),0);
        lv_obj_align(detail,LV_ALIGN_BOTTOM_LEFT,0,0);
        auto *arrow=lv_label_create(card);lv_label_set_text(arrow,LV_SYMBOL_RIGHT);lv_obj_align(arrow,LV_ALIGN_RIGHT_MID,0,0);
        lv_obj_add_event_cb(card,[](lv_event_t *e){
            selection=int(intptr_t(lv_event_get_user_data(e)));message.clear();redraw=true;
        },LV_EVENT_CLICKED,(void*)intptr_t(index));
    }
    if(items.size()>PageSize){
        text(content,("Page "+std::to_string(page+1)+" / "+std::to_string((items.size()+PageSize-1)/PageSize)).c_str(),true);
        if(page)button(content,"Previous apps",[](lv_event_t*){--page;redraw=true;});
        if((page+1)*PageSize<items.size())button(content,"More apps",[](lv_event_t*){++page;redraw=true;});
    }
    auto *browser=button(content,"Browser installation",[](lv_event_t*){browser_requested=true;closing=true;});
    lv_obj_set_style_bg_opa(browser,LV_OPA_TRANSP,0);
    lv_obj_set_style_shadow_width(browser,0,0);
}
void show(){
    requested=false;visible=true;selection=-1;page=0;native_launcher_release();
    auto *screen=lv_screen_active();lv_obj_set_style_bg_color(screen,lv_color_hex(0x172131),0);
    lv_obj_set_style_pad_all(screen,0,0);lv_obj_set_style_border_width(screen,0,0);
    lv_obj_remove_flag(screen,LV_OBJ_FLAG_SCROLLABLE);
    auto *bar=lv_obj_create(screen);lv_obj_set_size(bar,lv_pct(100),44);lv_obj_set_pos(bar,0,0);
    lv_obj_remove_flag(bar,LV_OBJ_FLAG_SCROLLABLE);lv_obj_set_style_pad_all(bar,0,0);
    lv_obj_set_style_bg_color(bar,lv_color_hex(0x14162a),0);lv_obj_set_style_border_width(bar,0,0);lv_obj_set_style_radius(bar,0,0);
    auto nav=[&](const char *symbol,lv_align_t alignment,lv_event_cb_t action){
        auto *b=lv_button_create(bar);lv_obj_set_size(b,56,44);lv_obj_align(b,alignment,alignment==LV_ALIGN_LEFT_MID?4:-4,0);
        lv_obj_set_style_bg_opa(b,LV_OPA_TRANSP,0);lv_obj_set_style_shadow_width(b,0,0);lv_obj_set_style_border_width(b,0,0);
        auto *label=lv_label_create(b);lv_label_set_text(label,symbol);lv_obj_center(label);
        lv_obj_add_event_cb(b,action,LV_EVENT_CLICKED,nullptr);return b;
    };
    nav(LV_SYMBOL_LEFT,LV_ALIGN_LEFT_MID,[](lv_event_t*){if(selection>=0&&!transfer){selection=-1;message.clear();redraw=true;}else closing=true;});
    refresh_button=nav(LV_SYMBOL_REFRESH,LV_ALIGN_RIGHT_MID,[](lv_event_t*){selection=-1;page=0;start();});
    auto *title=lv_label_create(bar);lv_label_set_text(title,i18n::tr("Add apps"));
    lv_obj_set_style_text_color(title,lv_color_hex(0x63e6be),0);lv_obj_center(title);
    content=lv_obj_create(screen);lv_obj_set_pos(content,0,44);
    lv_obj_set_size(content,lv_pct(100),lv_display_get_vertical_resolution(nullptr)-44);
    lv_obj_set_style_pad_all(content,10,0);lv_obj_set_style_pad_row(content,8,0);
    lv_obj_set_style_border_width(content,0,0);lv_obj_set_style_bg_opa(content,LV_OPA_TRANSP,0);
    lv_obj_set_flex_flow(content,LV_FLEX_FLOW_COLUMN);lv_obj_set_scroll_dir(content,LV_DIR_VER);
    installed_apps=store::installed();
    if(loaded){remember_catalog(true);message.clear();}
    else if(!transfer)start();
    redraw=true;
}
}
void create_tile(lv_obj_t *grid){
    auto *tile=lv_button_create(grid);lv_obj_set_size(tile,88,112);lv_obj_set_style_bg_opa(tile,LV_OPA_TRANSP,0);
    lv_obj_set_style_shadow_width(tile,0,0);lv_obj_set_style_pad_all(tile,2,0);
    auto *icon_box=lv_obj_create(tile);lv_obj_set_size(icon_box,68,68);lv_obj_align(icon_box,LV_ALIGN_TOP_MID,0,0);
    lv_obj_set_style_bg_color(icon_box,lv_color_hex(0x237e77),0);lv_obj_set_style_radius(icon_box,18,0);lv_obj_set_style_border_width(icon_box,0,0);
    lv_obj_remove_flag(icon_box,LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(icon_box,LV_OBJ_FLAG_SCROLLABLE);
    auto *icon=lv_label_create(icon_box);lv_label_set_text(icon,LV_SYMBOL_DOWNLOAD);lv_obj_set_style_text_font(icon,i18n::font24(),0);lv_obj_center(icon);
    auto *label=lv_label_create(tile);lv_label_set_text(label,i18n::tr("Add apps"));lv_obj_set_style_text_font(label,i18n::font14(),0);lv_obj_align(label,LV_ALIGN_BOTTOM_MID,0,0);
    badge=lv_label_create(tile);lv_label_set_text(badge,i18n::tr("New"));lv_obj_set_style_text_font(badge,i18n::font14(),0);lv_obj_set_style_text_color(badge,lv_color_hex(0x67e6c3),0);lv_obj_align(badge,LV_ALIGN_TOP_RIGHT,0,0);update_badge();
    lv_obj_add_event_cb(badge,[](lv_event_t*){badge=nullptr;},LV_EVENT_DELETE,nullptr);
    lv_obj_add_event_cb(tile,[](lv_event_t*){if(native_launcher_is_active()&&!native_app::busy())requested=true;},LV_EVENT_CLICKED,nullptr);
}
bool active(){return requested||visible;}
void close(){if(active())closing=true;}
void poll(){
    if(requested)show();
    if(closing){
        closing=false;requested=false;visible=false;if(transfer)transfer->cancelled.store(true);
        content=status_label=progress=refresh_button=nullptr;native_launcher_restore();
        if(browser_requested){browser_requested=false;store::request_manager();}
        if(!open_id.empty()){auto id=open_id;open_id.clear();native_app::launch_id(id.c_str());}
    }
    if(transfer&&!visible&&(!native_launcher_is_active()||native_app::busy()||ota_busy()))transfer->cancelled.store(true);
    if(transfer&&transfer->done.load(std::memory_order_acquire)){
        auto *t=transfer;transfer=nullptr;
        if(!t->cancelled.load()){
            if(!t->error.empty())message=t->error;
            else if(t->package){
                Manifest expected{t->item.id,t->item.version,"app.elf",t->item.name};
                if(store::install_signed(t->bytes,t->size,expected,message)){
                    message="Installed. Find it on the Installed apps screen.";installed_apps=store::installed();
                }
            }else {
                std::vector<catalog::Item> next;
                if(catalog::parse((char*)t->bytes,t->size,store::chip(),GADGET_OS_APP_CATALOG_URL,next,message)){
                    items=std::move(next);loaded=true;selection=-1;page=0;remember_catalog(visible);
                    std::stable_sort(items.begin(),items.end(),[](const auto &a,const auto &b){return new_ids.count(a.id)>new_ids.count(b.id);});
                    message.clear();
                }
            }
        }
        delete t;redraw=true;
    }
    if(visible&&redraw)render();
    if(progress&&transfer&&transfer->package)lv_bar_set_value(progress,int(transfer->received.load()*100/transfer->limit),LV_ANIM_OFF);
    if(!visible&&!transfer&&!native_app::busy()&&native_launcher_is_active()&&WiFi.status()==WL_CONNECTED&&!ota_busy()&&
       (!background_started?millis()>10000:millis()-last_check>(loaded?86400000U:900000U)))start();
}
}
#endif
