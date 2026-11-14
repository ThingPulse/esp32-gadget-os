#if GADGET_OS_NATIVE_APP_LOADER
#include "experimental/NativeAppStore.h"
#include "experimental/CompanionSetup.h"
#include "experimental/NativeApp.h"
#include "services/UpdateService.h"
#include "experimental/Companion.h"
#include "experimental/NativeUi.h"
#include "experimental/NativeSettings.h"
#include "experimental/NativeServices.h"
#include "apps/SettingsApp.h"
#include "services/AppUsage.h"
#include "experimental/NativePackage.h"
#include "experimental/NativeHttp.h"
#include "experimental/NativeSetup.h"
#include "experimental/NativeStore.h"
#include "gadget_app.h"
#include "AppRegistry.h"
#include "ui/UiSessionState.h"
#include <Arduino.h>
#include <ArduinoJson.h>
#include <LittleFS.h>
#include <esp_heap_caps.h>
#include <esp_timer.h>
#include <esp_memory_utils.h>
#include <esp_elf.h>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <memory>

namespace native_app {
namespace {
bool diagnostics=false;
#define NATIVE_TRACE(...) do { if(diagnostics) Serial.printf(__VA_ARGS__); } while(0)
struct Widget { lv_obj_t *obj=nullptr; bool label=false; gadget_callback cb=nullptr; void *user=nullptr; };
struct Timer { lv_timer_t *obj=nullptr; gadget_callback cb=nullptr; void *user=nullptr; };
struct Session {
    DeliveryGate gate;
    HttpDelivery http_delivery;
    esp_elf_t elf{};
    gadget_app_lifecycle app{};
    lv_obj_t *screen=nullptr;
    Widget widgets[16]; Timer timers[4];
    bool initialized=false, error=false;
    uint32_t start_ms=0, activity_ms=0;
    size_t sampled_min_internal=SIZE_MAX;
    unsigned callbacks=0;
} session;
bool active=false, pending=false,location_requested=false,settings_entry=false;
bool resume_settings_entry=false;
std::string resume_after_settings;
uint32_t retry_seconds=0;
std::string selected_id="native-demo";
unsigned cycle=0, cycles_left=0;
uint32_t next_cycle_ms=0;
bool reset_sent=false, close_sent=false;
char command[64]; size_t command_len=0;

void sample(const char *phase)
{
    if(!diagnostics)return;
    const uint32_t internal=MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT;
    lv_mem_monitor_t lv_memory{};
    lv_mem_monitor(&lv_memory);
    size_t free=heap_caps_get_free_size(internal);
    session.sampled_min_internal=std::min(session.sampled_min_internal,free);
    NATIVE_TRACE("[NATIVE] cycle=%u phase=%s internal=%u largest=%u exec=%u exec_largest=%u psram=%u psram_largest=%u boot_min_internal=%u sampled_cycle_min=%u callbacks=%u lvgl_total=%u lvgl_free=%u lvgl_largest=%u\n",
        cycle,phase,(unsigned)free,(unsigned)heap_caps_get_largest_free_block(internal),
        (unsigned)heap_caps_get_free_size(MALLOC_CAP_EXEC),(unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_EXEC),
        (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM),(unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM),
        (unsigned)heap_caps_get_minimum_free_size(internal),(unsigned)session.sampled_min_internal,session.callbacks,
        (unsigned)lv_memory.total_size,(unsigned)lv_memory.free_size,(unsigned)lv_memory.free_biggest_size);
}
void request_close() {
    if (active) { ui::suspend(); setup::stop(); session.gate.close(); session.http_delivery.clear(); http::cancel(cycle); }
}
void invoke(gadget_callback cb, void *user)
{
    if (!cb || !session.gate.enter()) return;
    ++session.callbacks;
    cb(user);
    session.gate.leave();
}
void button_event(lv_event_t *event)
{
    auto *w=static_cast<Widget *>(lv_event_get_user_data(event));
    invoke(w->cb,w->user);
}
void timer_event(lv_timer_t *timer)
{
    auto *t=static_cast<Timer *>(lv_timer_get_user_data(timer));
    invoke(t->cb,t->user);
}
void log(const char *s) { NATIVE_TRACE("[NATIVE app] %.160s\n",s?s:""); }
gadget_handle screen_create()
{
    if (session.gate.closing) return nullptr;
    return session.screen;
}
Widget *find_widget(gadget_handle h)
{
    for (auto &w:session.widgets) if (&w==h && w.obj) return &w;
    return nullptr;
}
gadget_handle widget_create(gadget_handle parent, const char *text, gadget_callback cb, void *user, bool label)
{
    if (session.gate.closing || parent!=session.screen || !text || strlen(text)>160) return nullptr;
    for (auto &w:session.widgets) if (!w.obj) {
        w.obj=label?lv_label_create(session.screen):lv_button_create(session.screen);
        if (!w.obj) return nullptr;
        w.label=label; w.cb=cb; w.user=user;
        lv_obj_set_width(w.obj,lv_pct(95));
        if (label) {
            lv_label_set_text(w.obj,text);
            lv_obj_set_style_text_align(w.obj,LV_TEXT_ALIGN_CENTER,0);
        } else {
            lv_obj_set_height(w.obj,58);
            auto *caption=lv_label_create(w.obj);
            lv_label_set_text(caption,text); lv_obj_center(caption);
            lv_obj_add_event_cb(w.obj,button_event,LV_EVENT_CLICKED,&w);
        }
        return &w;
    }
    return nullptr;
}
gadget_handle label_create(gadget_handle p,const char *s) { return widget_create(p,s,nullptr,nullptr,true); }
gadget_handle button_create(gadget_handle p,const char *s,gadget_callback cb,void *u) { return widget_create(p,s,cb,u,false); }
int label_set(gadget_handle h,const char *text)
{
    Widget *w=find_widget(h);
    if (session.gate.closing || !w || !w->label || !text || strlen(text)>160) return -1;
    lv_label_set_text(w->obj,text); return 0;
}
gadget_handle timer_create(uint32_t ms,gadget_callback cb,void *user)
{
    if (session.gate.closing || !cb || ms<20 || ms>60000) return nullptr;
    for (auto &t:session.timers) if (!t.obj) {
        t.cb=cb; t.user=user; t.obj=lv_timer_create(timer_event,ms,&t);
        return t.obj?&t:nullptr;
    }
    return nullptr;
}
int32_t http_get(const char *url,gadget_http_callback callback,void *user)
{
    if (!active || session.gate.closing || !callback) return GADGET_HTTP_INVALID;
    int32_t result=http::start(url,cycle);
    if (!result) session.http_delivery={cycle,callback,user};
    return result;
}
int32_t setting_get(const char *k,char *v,uint32_t n){return session.gate.closing?-1:settings::get(k,v,n);}
int32_t setting_set(const char *k,const char *v){return session.gate.closing?-1:settings::set(k,v);}
int32_t companion_channel_read(const char *channel,char *json,uint32_t capacity,uint32_t *age_ms){
    if(session.gate.closing||selected_id.empty())return -1;
    return companion::read_channel(selected_id.c_str(),channel,json,capacity,age_ms);
}
int32_t activity_read(char *v,uint32_t n){return session.gate.closing||selected_id!="agent-usage"?-1:companion::read_activity(v,n);}
int32_t companion_read(char *v,uint32_t n){return session.gate.closing||selected_id!="agent-usage"?-1:companion::read_usage(v,n);}
int32_t system_read(char *v,uint32_t n){return session.gate.closing||selected_id!="system-monitor"?-1:companion::read_system(v,n);}
int32_t system_refresh(){return session.gate.closing||selected_id!="system-monitor"?-1:companion::refresh_system();}
int32_t json_string(const char *s,uint32_t n,const char *key,char *out,uint32_t cap){if(!s||!n||n>2048||!key||strlen(key)>48||!out||!cap||cap>2049)return -1;JsonDocument d;if(deserializeJson(d,s,n)||!d[key].is<const char*>())return -1;const char *v=d[key];size_t len=strlen(v);if(len>=cap)return -1;memcpy(out,v,len+1);return len;}
int32_t extended_http(const char*url,uint32_t cap,gadget_http_callback cb,void*user){if(!active||session.gate.closing||!cb)return GADGET_HTTP_INVALID;int32_t r=http::start(url,cycle,cap);if(!r)session.http_delivery={cycle,cb,user};return r;}
void open_location(){if(active&&!session.gate.closing){location_requested=true;request_close();}}
int32_t request_http(const gadget_http_request*r,gadget_http_callback cb,void*u){if(!active||session.gate.closing||!cb)return GADGET_HTTP_INVALID;int32_t result=http::start_request(r,cycle);if(!result)session.http_delivery={cycle,cb,u};return result;}
int32_t start_setup(const void*p,uint32_t n,gadget_setup_callback cb,void*u,char*out,uint32_t cap){if(!active||session.gate.closing)return -1;return setup::start(p,n,cb,u,out,cap);}
const gadget_services_api service_api={sizeof(service_api),services::environment,services::format_time,services::parse_time,services::json_open,services::json_get,services::json_count,services::json_close,services::cache_read,services::cache_write,open_location,extended_http,[]()->uint32_t{return retry_seconds;},services::json_number,services::json_real,services::format_number,services::monotonic_ms,&services::math_api,settings::config_read,settings::config_write,request_http,start_setup,setup::stop,setup::is_active};
const gadget_host_api api={sizeof(api),1,10,log,screen_create,label_create,button_create,label_set,timer_create,request_close,http_get,json_number_scaled,&ui::api,setting_get,setting_set,companion_read,system_read,system_refresh,json_string,&service_api,activity_read,companion_channel_read,[]()->uint32_t{return settings_entry?GADGET_LAUNCH_SETTINGS:GADGET_LAUNCH_NORMAL;}};
void resources_clear()
{
    ui::end(); settings::end();services::end();
    session.http_delivery.clear(); http::cancel(cycle); setup::stop();
    for (auto &t:session.timers) { if (t.obj) lv_timer_delete(t.obj); t={}; }
    // Only host trampoline pointers are registered with LVGL. Destroy their
    // owning object tree before releasing the executable and callback records.
    for (auto &w:session.widgets) { if (w.obj) lv_obj_delete(w.obj); w={}; }
}
void unload()
{
    resources_clear();
    if (session.initialized && session.app.shutdown) {
        // gate remains closed: shutdown cannot schedule callbacks or new UI.
        ++session.gate.depth;
        session.app.shutdown();
        --session.gate.depth;
    }
    session.app={}; session.initialized=false;
    esp_elf_deinit(&session.elf);
}
void show_error(const std::string &message)
{
    session.gate.close(); unload();
    session.error=true; cycles_left=0;
    lv_obj_set_flex_flow(session.screen,LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(session.screen,LV_FLEX_ALIGN_CENTER,LV_FLEX_ALIGN_CENTER,LV_FLEX_ALIGN_CENTER);
    NATIVE_TRACE("[NATIVE] ERROR %.160s\n",message.c_str());
    sample("load-failed-cleaned");
    auto *label=lv_label_create(session.screen);
    lv_obj_set_width(label,lv_pct(95)); lv_label_set_text(label,message.c_str());
    auto *button=lv_button_create(session.screen);
    lv_obj_set_size(button,200,58);
    auto *caption=lv_label_create(button); lv_label_set_text(caption,settings_entry?"Back to app settings":"Back to launcher"); lv_obj_center(caption);
    lv_obj_add_event_cb(button,[](lv_event_t *){session.error=false;},LV_EVENT_CLICKED,nullptr);
}
void open_now()
{
    pending=false; active=true; ++cycle; session=Session{};
    reset_sent=close_sent=false;
    sample("launcher-idle");
    const int64_t start=esp_timer_get_time();
    native_launcher_release();
    session.screen=lv_screen_active();
    lv_obj_set_style_bg_color(session.screen,lv_color_hex(0x172131),0);
    lv_obj_set_style_bg_opa(session.screen,LV_OPA_COVER,0);
    lv_obj_set_style_text_color(session.screen,lv_color_hex(0xf1f3f5),0);
    lv_obj_set_flex_flow(session.screen,LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(session.screen,LV_FLEX_ALIGN_CENTER,LV_FLEX_ALIGN_CENTER,LV_FLEX_ALIGN_CENTER);
    // Paint acknowledgement before synchronous package validation and relocation.
    auto *loading=lv_label_create(session.screen);
    lv_label_set_text(loading,"Opening app...");
    lv_refr_now(nullptr);
    sample("launcher-released");
    Package package; std::string error;
    auto image=store::read(store::path(selected_id).c_str(),package,error);
    lv_obj_delete(loading);
    if(!image){show_error(error);return;}
    if(package.manifest.id!=selected_id){show_error("Package ID mismatch");return;}
    if(settings_entry&&!package.manifest.settings){show_error("App has no settings entry");return;}
    esp_elf_init(&session.elf);
    int result=esp_elf_relocate(&session.elf,image.get()+package.elf_offset);
    sample("relocated-with-file-buffer");
    // Release the file buffer before measuring the steady executable allocation.
    image.reset();
    if (result) { show_error("ELF relocation/allocation failed; see serial log"); return; }
    NATIVE_TRACE("[NATIVE] text_bytes=%u data_allocation=%u text_ptr=%p data_ptr=%p\n",
        (unsigned)session.elf.sec[ELF_SEC_TEXT].size,
        (unsigned)(session.elf.pdata?heap_caps_get_allocated_size(session.elf.pdata):0),session.elf.ptext,session.elf.pdata);
    // Executable pointers can use an IRAM alias not recognized by
    // heap_caps_get_allocated_size(). Measure its cost with heap deltas.
    NATIVE_TRACE("[NATIVE] text_psram=%u data_psram=%u\n",
        esp_ptr_external_ram(session.elf.ptext)?1:0,esp_ptr_external_ram(session.elf.pdata)?1:0);
    sample("executable-loaded");
    ui::begin(session.screen,invoke); settings::begin(selected_id.c_str());services::begin(selected_id.c_str());
    session.app={sizeof(session.app),1,2,nullptr};
    char *args[]={reinterpret_cast<char *>(const_cast<gadget_host_api *>(&api)),reinterpret_cast<char *>(&session.app)};
    session.initialized=true;
    session.gate.enter();
    result=session.elf.entry(2,args);
    session.gate.leave();
    if (result || session.app.struct_size!=sizeof(session.app) || session.app.major!=1 || session.app.minor>10 || !session.app.shutdown) {
        show_error("App initialization/API handshake failed"); return;
    }
    AppUsage::native_start(package.manifest.id.c_str(),package.manifest.version.c_str(),package.trusted);
    sample("app-ui-created");
    NATIVE_TRACE("[NATIVE] cycle=%u load_us=%lld\n",cycle,(long long)(esp_timer_get_time()-start));
    session.start_ms=session.activity_ms=millis();
}
void finish()
{
    AppUsage::stop();
    const int64_t start=esp_timer_get_time();
    unload();
    NATIVE_TRACE("[NATIVE] cycle=%u unload_us=%lld\n",cycle,(long long)(esp_timer_get_time()-start));
    sample("shutdown-unloaded");
    native_launcher_restore(); active=false; session.screen=nullptr;
    if(location_requested){location_requested=false;resume_after_settings=selected_id;resume_settings_entry=settings_entry;settings_launch_with_route("settings_weather");}
    else if(settings_entry){settings_entry=false;settings_launch_with_route("settings_apps");}
    sample("launcher-restored");
    NATIVE_TRACE("[NATIVE] cycle=%u unload_restore_us=%lld\n",cycle,(long long)(esp_timer_get_time()-start));
    if (cycles_left) {
        --cycles_left;
        if (!cycles_left) NATIVE_TRACE("[NATIVE] CYCLES COMPLETE\n");
        else next_cycle_ms=millis()+500;
    }
}
void receive_command()
{
    while (Serial.available()) {
        char c=Serial.read();
        if (c=='\n') {
            command[command_len]=0;
            if (!strcmp(command,"native trace on")) diagnostics=true;
            else if (!strcmp(command,"native trace off")) diagnostics=false;
            else if (!strcmp(command,"native open")) launch();
            else if (!strcmp(command,"native iss")) launch_iss();
            else if (!strcmp(command,"native manager")) store::request_manager();
            else if (!strcmp(command,"native manager close")) store::close_manager();
            else if (!strncmp(command,"native run ",11)) launch_id(command+11);
            else if (active && selected_id=="native-iss" && !session.error && !session.gate.closing &&
                     (!strcmp(command,"native iss refresh") || !strcmp(command,"native iss close"))) {
                const unsigned index=!strcmp(command,"native iss refresh")?3:4;
                if (session.widgets[index].obj) lv_obj_send_event(session.widgets[index].obj,LV_EVENT_CLICKED,nullptr);
            }
            else if (!strcmp(command,"native close")) { app_store::close(); pending=false; request_close(); session.error=false; cycles_left=0; }
            else if (!strcmp(command,"native test 100") && !busy() && native_launcher_is_active()) {
                diagnostics=true; launch(); cycles_left=100;
            }
            command_len=0;
        } else if (c!='\r') {
            if (command_len<sizeof(command)-1) command[command_len++]=c;
            else command_len=0;
        }
    }
}
}
bool trace_enabled(){return diagnostics;}
bool busy() { return active || pending || cycles_left || store::managing() || app_store::active() || companion::active() || companion_setup::active(); }
bool running(const char *id) {return (active||pending||cycles_left)&&selected_id==id;}
void launch_id(const char *id) {
    if(!busy()&&native_launcher_is_active()&&id&&valid_app_id(id)){
        if(companion_setup::required(id)&&!companion::paired()){companion_setup::request(id);return;}
        selected_id=id;settings_entry=false;pending=true;
    }
}
bool launch_settings_id(const char *id){
    const auto update=UpdateService::instance().state();
    if(update==UpdateService::State::Downloading||update==UpdateService::State::ReadyToReboot)return false;
    if(!id||busy()||!settings_apps_active())return false;
    for(const auto &app:store::installed())if(app.launchable&&app.manifest.id==id&&app.manifest.settings){
        selected_id=id;settings_entry=true;pending=true;return true;
    }
    return false;
}
void launch() { launch_id("native-demo"); }
void launch_iss() { launch_id("native-iss"); }
void poll()
{
    setup::poll();
    receive_command();
    companion::poll();
    companion_setup::poll();
    store::poll();
    app_store::poll();
    if(!resume_after_settings.empty()&&native_launcher_is_active()&&!busy()){auto id=resume_after_settings;resume_after_settings.clear();selected_id=id;settings_entry=resume_settings_entry;pending=true;}
    if (pending) open_now();
    if (std::unique_ptr<http::Result> r{http::take_completed()}; r) {
        retry_seconds=r->retry_seconds;
        bool delivered=active && !session.error && session.http_delivery.deliver(
            r->generation,session.gate,r->status,r->body,r->size);
        if (delivered) ++session.callbacks;
        NATIVE_TRACE("[NATIVE HTTP] generation=%u phase=complete status=%ld bytes=%u elapsed_ms=%u delivered=%u\n",
            r->generation,(long)r->status,r->size,r->elapsed_ms,delivered?1:0);
        r.reset();
        sample(delivered?"http-delivered":"http-discarded");
    }
    if (!active) {
        if (cycles_left && int32_t(millis()-next_cycle_ms)>=0) { selected_id="native-demo"; pending=true; }
        return;
    }
    session.sampled_min_internal=std::min(session.sampled_min_internal,heap_caps_get_free_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT));
    if (session.gate.can_unload() && !session.error) { finish(); return; }
    if (session.error) return;
    if (uint32_t(millis()-session.activity_ms)>=1000) { sample("activity"); session.activity_ms=millis(); }
    if (cycles_left) {
        // Deliver actual LVGL button events to test app-owned reset and close
        // callbacks. finish() runs on a later poll, after this stack unwinds.
        if (!reset_sent && uint32_t(millis()-session.start_ms)>=600) {
            reset_sent=true; lv_obj_send_event(session.widgets[2].obj,LV_EVENT_CLICKED,nullptr);
        }
        // Network work can coalesce LVGL timer ticks. Keep the same callback
        // acceptance threshold without closing a healthy app before it reaches it.
        if (uint32_t(millis()-session.start_ms)>=5000 && session.callbacks<6) {
            show_error("Cycle test timed out waiting for timer callbacks");return;
        }
        if (!close_sent && uint32_t(millis()-session.start_ms)>=1500 && session.callbacks>=6) {
            close_sent=true; lv_obj_send_event(session.widgets[3].obj,LV_EVENT_CLICKED,nullptr);
        }
    }
}
}
#endif
