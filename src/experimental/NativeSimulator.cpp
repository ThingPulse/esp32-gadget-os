#if GADGET_OS_APP_STORE_SIMULATOR
#include "experimental/NativeSimulator.h"
#include "experimental/NativeStore.h"
#include "experimental/NativeApp.h"
#include "experimental/NativeAppStore.h"
#include "experimental/NativeServices.h"
#include "ui/I18n.h"
#include <algorithm>
#include <fstream>
#include <iterator>
#include <cstdio>
#include <sys/stat.h>

void agent_usage_simulator_launch();
void system_monitor_simulator_launch();
void iss_tracker_simulator_launch();

namespace native_app {
namespace {
constexpr const char *state_path=".pio/simulator-apps.json";
std::string pending_id, running_id;
lv_obj_t *preview_home=nullptr;

bool save(const std::vector<store::Installed> &apps,std::string &error){
    mkdir(".pio",0700);
    JsonDocument doc;auto array=doc.to<JsonArray>();
    for(const auto &app:apps){auto value=array.add<JsonObject>();value["id"]=app.manifest.id;value["version"]=app.manifest.version;}
    std::string json;serializeJson(doc,json);
    const std::string temp=std::string(state_path)+".tmp";
    std::ofstream file(temp,std::ios::binary);file<<json;file.close();
    if(!file||std::rename(temp.c_str(),state_path)!=0){
        std::remove(temp.c_str());error="Could not save installed apps.";return false;
    }
    return true;
}
}
namespace simulator {
std::vector<catalog::Item> catalog(){return {
#include "SimulatorCatalog.inc"
};}
bool install(const catalog::Item &item,std::string &error){
    auto available=catalog();
    auto found=std::find_if(available.begin(),available.end(),[&](const auto &a){return a.id==item.id&&a.version==item.version;});
    if(found==available.end()){error="App is not available in the simulator.";return false;}
    auto apps=store::installed();
    apps.erase(std::remove_if(apps.begin(),apps.end(),[&](const auto &a){return a.manifest.id==item.id;}),apps.end());
    apps.push_back({{item.id,item.version,"",item.name},true});
    return save(apps,error);
}
}
namespace store {
std::vector<Installed> installed(){
    std::ifstream file(state_path);std::string json((std::istreambuf_iterator<char>(file)),{});
    JsonDocument doc;std::vector<Installed> result;
    if(json.size()>4096||deserializeJson(doc,json)||!doc.is<JsonArray>())return result;
    // Only known desktop adapters can be installed; ignore stale/unknown entries.
    for(const auto &item:simulator::catalog())for(auto value:doc.as<JsonArray>()){
        if(item.id==std::string(value["id"]|"")){
            result.push_back({{item.id,value["version"]|"", "",item.name},true});break;
        }
    }
    return result;
}
bool uninstall(const std::string &id,std::string &error){
    if(native_app::running(id.c_str())){error="Close the running app first.";return false;}
    auto apps=installed();auto end=std::remove_if(apps.begin(),apps.end(),[&](const auto &a){return a.manifest.id==id;});
    if(end==apps.end()){error="App is not installed.";return false;}
    apps.erase(end,apps.end());if(!save(apps,error))return false;
    // Desktop adapters use isolated preview settings/cache namespaces.
    std::remove((".pio/native-app-settings/"+id+"-preview.json").c_str());
    services::erase_cache((id+"-preview").c_str());
    return true;
}
void create_tiles(lv_obj_t *grid){
    auto apps=installed();
    if(apps.empty()){
        auto *label=lv_label_create(grid);lv_obj_set_width(label,lv_pct(100));
        lv_label_set_text(label,i18n::tr("No apps installed yet. Open Add apps on the first screen."));
        lv_obj_set_style_text_font(label,i18n::font14(),0);
    }
    for(const auto &app:apps){
        auto *id=new std::string(app.manifest.id);
        auto *tile=lv_button_create(grid);lv_obj_set_size(tile,88,112);
        lv_obj_set_style_bg_opa(tile,LV_OPA_TRANSP,0);lv_obj_set_style_bg_opa(tile,LV_OPA_COVER,LV_STATE_PRESSED);
        lv_obj_set_style_bg_color(tile,lv_color_hex(0x38706f),LV_STATE_PRESSED);
        lv_obj_set_style_shadow_width(tile,0,0);lv_obj_set_style_pad_all(tile,2,0);
        auto *icon=lv_label_create(tile);
        lv_label_set_text(icon,*id=="iss-tracker"?LV_SYMBOL_GPS:*id=="agent-usage"?LV_SYMBOL_CHARGE:LV_SYMBOL_LIST);
        lv_obj_set_style_text_font(icon,&lv_font_montserrat_32,0);lv_obj_align(icon,LV_ALIGN_TOP_MID,0,14);
        auto *name=lv_label_create(tile);lv_label_set_text(name,app.manifest.name.c_str());lv_obj_set_width(name,84);
        lv_obj_set_style_text_font(name,i18n::font14(),0);lv_obj_set_height(name,2*i18n::font14()->line_height);
        lv_label_set_long_mode(name,LV_LABEL_LONG_DOT);lv_obj_set_style_text_align(name,LV_TEXT_ALIGN_CENTER,0);
        lv_obj_align(name,LV_ALIGN_BOTTOM_MID,0,0);
        lv_obj_add_event_cb(tile,[](lv_event_t *e){launch_id(static_cast<std::string*>(lv_event_get_user_data(e))->c_str());},LV_EVENT_CLICKED,id);
        lv_obj_add_event_cb(tile,[](lv_event_t *e){delete static_cast<std::string*>(lv_event_get_user_data(e));},LV_EVENT_DELETE,id);
    }
}
}
void launch_id(const char *id){
    if(!id||busy()||!native_launcher_is_active())return;
    for(const auto &app:store::installed())if(app.manifest.id==id){pending_id=id;break;}
}
bool running(const char *id){return id&&running_id==id&&preview_home&&lv_screen_active()!=preview_home;}
bool busy(){return app_store::active()||!pending_id.empty()||(preview_home&&lv_screen_active()!=preview_home);}
void poll(){
    if(preview_home&&lv_screen_active()==preview_home){preview_home=nullptr;running_id.clear();}
    app_store::poll();
    if(!pending_id.empty()){
        auto id=pending_id;pending_id.clear();preview_home=lv_screen_active();running_id=id;
        if(id=="agent-usage")agent_usage_simulator_launch();
        else if(id=="system-monitor")system_monitor_simulator_launch();
        else if(id=="iss-tracker")iss_tracker_simulator_launch();
    }
}
}
#endif
