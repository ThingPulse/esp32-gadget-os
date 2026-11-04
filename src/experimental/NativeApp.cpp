#if GADGET_OS_NATIVE_APP_LOADER
#include "experimental/NativeApp.h"
#include "experimental/NativePackage.h"
#include "experimental/NativeHttp.h"
#include "gadget_app.h"
#include "AppRegistry.h"
#include "ui/UiSessionState.h"
#include <Arduino.h>
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

APP_REGISTER_EXPERIMENTAL_COLOR_RANK("Native demo", LV_SYMBOL_FILE, native_app::launch, 0x8561c5, 95);
namespace {
lv_obj_t *iss_icon(lv_obj_t *parent,lv_coord_t size)
{
    auto *icon=lv_obj_create(parent); lv_obj_remove_style_all(icon);
    lv_obj_set_size(icon,size,size); lv_obj_remove_flag(icon,static_cast<lv_obj_flag_t>(LV_OBJ_FLAG_CLICKABLE|LV_OBJ_FLAG_SCROLLABLE));
    auto box=[&](int x,int y,int w,int h,uint32_t color) {
        auto *part=lv_obj_create(icon); lv_obj_remove_style_all(part);
        lv_obj_set_pos(part,x*size/40,y*size/40); lv_obj_set_size(part,w*size/40,h*size/40);
        lv_obj_set_style_bg_color(part,lv_color_hex(color),0); lv_obj_set_style_bg_opa(part,LV_OPA_COVER,0);
        lv_obj_remove_flag(part,static_cast<lv_obj_flag_t>(LV_OBJ_FLAG_CLICKABLE|LV_OBJ_FLAG_SCROLLABLE));
    };
    box(2,19,36,2,0xffffff); box(2,8,11,24,0x63d5d0); box(27,8,11,24,0x63d5d0);
    box(16,12,8,16,0xffffff); box(19,3,2,9,0xffffff);
    return icon;
}
const bool iss_registered=AppRegistry::instance().registerApp(
    {"ISS Position",LV_SYMBOL_GPS,native_app::launch_iss,0x237e77,96,AppAvailability::Experimental,iss_icon});
}
namespace native_app {
namespace {
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
bool active=false, pending=false;
bool pending_iss=false;
unsigned cycle=0, cycles_left=0;
uint32_t next_cycle_ms=0;
bool reset_sent=false, close_sent=false;
char command[64]; size_t command_len=0;

void sample(const char *phase)
{
    const uint32_t internal=MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT;
    lv_mem_monitor_t lv_memory{};
    lv_mem_monitor(&lv_memory);
    size_t free=heap_caps_get_free_size(internal);
    session.sampled_min_internal=std::min(session.sampled_min_internal,free);
    Serial.printf("[NATIVE] cycle=%u phase=%s internal=%u largest=%u exec=%u exec_largest=%u psram=%u psram_largest=%u boot_min_internal=%u sampled_cycle_min=%u callbacks=%u lvgl_total=%u lvgl_free=%u lvgl_largest=%u\n",
        cycle,phase,(unsigned)free,(unsigned)heap_caps_get_largest_free_block(internal),
        (unsigned)heap_caps_get_free_size(MALLOC_CAP_EXEC),(unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_EXEC),
        (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM),(unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM),
        (unsigned)heap_caps_get_minimum_free_size(internal),(unsigned)session.sampled_min_internal,session.callbacks,
        (unsigned)lv_memory.total_size,(unsigned)lv_memory.free_size,(unsigned)lv_memory.free_biggest_size);
}
void request_close() {
    if (active) { session.gate.close(); session.http_delivery.clear(); http::cancel(cycle); }
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
void log(const char *s) { Serial.printf("[NATIVE app] %.160s\n",s?s:""); }
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
const gadget_host_api api={sizeof(api),1,1,log,screen_create,label_create,button_create,label_set,timer_create,request_close,http_get,json_number_scaled};
void resources_clear()
{
    session.http_delivery.clear(); http::cancel(cycle);
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
    Serial.printf("[NATIVE] ERROR %.160s\n",message.c_str());
    sample("load-failed-cleaned");
    auto *label=lv_label_create(session.screen);
    lv_obj_set_width(label,lv_pct(95)); lv_label_set_text(label,message.c_str());
    auto *button=lv_button_create(session.screen);
    lv_obj_set_size(button,200,58);
    auto *caption=lv_label_create(button); lv_label_set_text(caption,"Back to launcher"); lv_obj_center(caption);
    lv_obj_add_event_cb(button,[](lv_event_t *){session.error=false;},LV_EVENT_CLICKED,nullptr);
}
bool read_bounded(const char *path, size_t limit, std::string &bytes)
{
    auto f=LittleFS.open(path,"r");
    if (!f || f.isDirectory() || !f.size() || f.size()>limit) return false;
    bytes.resize(f.size());
    return f.readBytes(&bytes[0],bytes.size())==bytes.size();
}
void open_now()
{
    pending=false; active=true; ++cycle; session=Session{};
    reset_sent=close_sent=false;
    sample("launcher-idle");
    const int64_t start=esp_timer_get_time();
    native_launcher_release();
    session.screen=lv_screen_active();
    lv_obj_set_flex_flow(session.screen,LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(session.screen,LV_FLEX_ALIGN_CENTER,LV_FLEX_ALIGN_CENTER,LV_FLEX_ALIGN_CENTER);
    sample("launcher-released");
    std::string bytes,error; Manifest manifest;
#if CONFIG_IDF_TARGET_ESP32S3
    const char *chip="esp32s3";
#else
    const char *chip="esp32";
#endif
    const char *id=pending_iss?"native-iss":"native-demo";
    const std::string base=std::string("/apps/")+id+"/";
    if (!read_bounded((base+"manifest.json").c_str(),2048,bytes)) { show_error("Missing or oversized "+base+"manifest.json"); return; }
    if (!parse_manifest(bytes,chip,manifest,error)) { show_error(error); return; }
    if (manifest.id!=id) { show_error("Package ID does not match selected launcher app"); return; }
    std::string().swap(bytes);
    auto file=LittleFS.open((base+"app.elf").c_str(),"r");
    if (!file || file.isDirectory() || !file.size() || file.size()>max_elf_bytes) {
        show_error("Missing or oversized app.elf"); return;
    }
    const size_t file_size=file.size();
    std::unique_ptr<uint8_t,decltype(&heap_caps_free)> image(
        static_cast<uint8_t *>(heap_caps_malloc(file_size,MALLOC_CAP_8BIT)),heap_caps_free);
    if (!image) { show_error("Not enough memory to read app.elf"); return; }
    if (file.read(image.get(),file_size)!=file_size) { image.reset(); show_error("Cannot read app.elf"); return; }
    file.close();
    if (!validate_elf(image.get(),file_size,error)) { image.reset(); show_error(error); return; }
    esp_elf_init(&session.elf);
    int result=esp_elf_relocate(&session.elf,image.get());
    sample("relocated-with-file-buffer");
    // Release the file buffer before measuring the steady executable allocation.
    image.reset();
    if (result) { show_error("ELF relocation/allocation failed; see serial log"); return; }
    Serial.printf("[NATIVE] text_bytes=%u data_allocation=%u text_ptr=%p data_ptr=%p\n",
        (unsigned)session.elf.sec[ELF_SEC_TEXT].size,
        (unsigned)(session.elf.pdata?heap_caps_get_allocated_size(session.elf.pdata):0),session.elf.ptext,session.elf.pdata);
    // Executable pointers can use an IRAM alias not recognized by
    // heap_caps_get_allocated_size(). Measure its cost with heap deltas.
    Serial.printf("[NATIVE] text_psram=%u data_psram=%u\n",
        esp_ptr_external_ram(session.elf.ptext)?1:0,esp_ptr_external_ram(session.elf.pdata)?1:0);
    sample("executable-loaded");
    session.app={sizeof(session.app),1,1,nullptr};
    char *args[]={reinterpret_cast<char *>(const_cast<gadget_host_api *>(&api)),reinterpret_cast<char *>(&session.app)};
    session.initialized=true;
    session.gate.enter();
    result=session.elf.entry(2,args);
    session.gate.leave();
    if (result || session.app.struct_size!=sizeof(session.app) || session.app.major!=1 || session.app.minor>1 || !session.app.shutdown) {
        show_error("App initialization/API handshake failed"); return;
    }
    sample("app-ui-created");
    Serial.printf("[NATIVE] cycle=%u load_us=%lld\n",cycle,(long long)(esp_timer_get_time()-start));
    session.start_ms=session.activity_ms=millis();
}
void finish()
{
    const int64_t start=esp_timer_get_time();
    unload();
    Serial.printf("[NATIVE] cycle=%u unload_us=%lld\n",cycle,(long long)(esp_timer_get_time()-start));
    sample("shutdown-unloaded");
    native_launcher_restore(); active=false; session.screen=nullptr;
    sample("launcher-restored");
    Serial.printf("[NATIVE] cycle=%u unload_restore_us=%lld\n",cycle,(long long)(esp_timer_get_time()-start));
    if (cycles_left) {
        --cycles_left;
        if (!cycles_left) Serial.printf("[NATIVE] CYCLES COMPLETE\n");
        else next_cycle_ms=millis()+500;
    }
}
void receive_command()
{
    while (Serial.available()) {
        char c=Serial.read();
        if (c=='\n') {
            command[command_len]=0;
            if (!strcmp(command,"native open")) launch();
            else if (!strcmp(command,"native iss")) launch_iss();
            else if (active && pending_iss && !session.error && !session.gate.closing &&
                     (!strcmp(command,"native iss refresh") || !strcmp(command,"native iss close"))) {
                const unsigned index=!strcmp(command,"native iss refresh")?3:4;
                if (session.widgets[index].obj) lv_obj_send_event(session.widgets[index].obj,LV_EVENT_CLICKED,nullptr);
            }
            else if (!strcmp(command,"native close")) { pending=false; request_close(); session.error=false; cycles_left=0; }
            else if (!strcmp(command,"native test 100") && !busy() && native_launcher_is_active()) {
                cycles_left=100; launch();
            }
            command_len=0;
        } else if (c!='\r') {
            if (command_len<sizeof(command)-1) command[command_len++]=c;
            else command_len=0;
        }
    }
}
}
bool busy() { return active || pending || cycles_left; }
void launch() { if (!active && !pending && native_launcher_is_active()) { pending=true; pending_iss=false; } }
void launch_iss() { if (!active && !pending && native_launcher_is_active()) { pending=true; pending_iss=true; } }
void poll()
{
    receive_command();
    if (pending) open_now();
    if (std::unique_ptr<http::Result> r{http::take_completed()}; r) {
        bool delivered=active && !session.error && session.http_delivery.deliver(
            r->generation,session.gate,r->status,r->body,r->size);
        if (delivered) ++session.callbacks;
        Serial.printf("[NATIVE HTTP] generation=%u phase=complete status=%ld bytes=%u elapsed_ms=%u delivered=%u\n",
            r->generation,(long)r->status,r->size,r->elapsed_ms,delivered?1:0);
        r.reset();
        sample(delivered?"http-delivered":"http-discarded");
    }
    if (!active) {
        if (cycles_left && int32_t(millis()-next_cycle_ms)>=0) launch();
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
        if (!close_sent && uint32_t(millis()-session.start_ms)>=1500) {
            close_sent=true; lv_obj_send_event(session.widgets[3].obj,LV_EVENT_CLICKED,nullptr);
        }
    }
}
}
#endif
