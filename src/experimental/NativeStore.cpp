#if GADGET_OS_NATIVE_APP_LOADER
#include "experimental/NativeStore.h"
#include "experimental/NativeApp.h"
#include "AppRegistry.h"
#include "services/UpdateService.h"
#include <Arduino.h>
#include <src/misc/cache/instance/lv_image_cache.h>
#include <ArduinoJson.h>
#include <LittleFS.h>
#include <WiFi.h>
#include <esp_heap_caps.h>
#include <esp_system.h>
#include <lwip/sockets.h>
#include <cerrno>
#include <algorithm>
#include <cstring>
#include <cctype>
#include "NativeStorePage.inc"

APP_REGISTER_EXPERIMENTAL_COLOR_RANK("Apps",LV_SYMBOL_DOWNLOAD,native_app::store::request_manager,0x237e77,94);
namespace native_app::store {
namespace {
constexpr unsigned MaxApps=8;
constexpr const char *Stage="/apps/.incoming.gapp";
struct Entry {Manifest manifest; size_t icon_offset=0;};
Entry entries[MaxApps]; unsigned count=0;
bool dirty=true,requested=false,visible=false,close_requested=false,serving=false;
std::string key;
uint32_t expires=0;
lv_obj_t *notice=nullptr;
WiFiServer server(8081); WiFiClient peer; File upload;
std::string headers,output; size_t sent=0,remaining=0;
bool uploading=false,peer_active=false;
uint32_t deadline=0,started_at=0;
void drop() {
    peer.stop(); peer_active=false; if(upload)upload.close(); LittleFS.remove(Stage);
    headers.clear(); output.clear(); sent=remaining=0; uploading=false;
}
std::string address() {return "http://"+std::string(WiFi.localIP().toString().c_str())+":8081";}
void respond(int status,const std::string &body,const char *type="application/json") {
    if(upload)upload.close(); uploading=false; LittleFS.remove(Stage);
    output="HTTP/1.1 "+std::to_string(status)+" Response\r\nContent-Type: "+type+
      "\r\nContent-Length: "+std::to_string(body.size())+
      "\r\nCache-Control: no-store\r\nX-Content-Type-Options: nosniff\r\nReferrer-Policy: no-referrer\r\nContent-Security-Policy: default-src 'self'; script-src 'unsafe-inline'; style-src 'unsafe-inline'; connect-src 'self'; frame-ancestors 'none'; base-uri 'none'\r\nConnection: close\r\n\r\n"+body;
    sent=0;
}
void error(int status,const std::string &message) {
    JsonDocument d; d["error"]=message; std::string b; serializeJson(d,b); respond(status,b);
}
bool ota_busy() {auto s=UpdateService::instance().state();return s==UpdateService::State::Downloading||s==UpdateService::State::ReadyToReboot;}
void scan() {
    count=0; auto dir=LittleFS.open("/apps/installed");
    if(!dir||!dir.isDirectory())return;
    for(auto f=dir.openNextFile();f;f=dir.openNextFile()) {
        std::string filename=f.name(); if(f.isDirectory()||filename.size()<6||filename.substr(filename.size()-5)!=".gapp")continue;
        auto slash=filename.rfind('/'); if(slash!=std::string::npos)filename=filename.substr(slash+1);
        std::string id=filename.substr(0,filename.size()-5); if(!valid_app_id(id))continue;
        Package package;std::string why;auto data=read(path(id).c_str(),package,why);
        if(!data||package.manifest.id!=id)continue;
        if(count<MaxApps)entries[count++]={package.manifest,package.icon_offset};
    }
    std::sort(entries,entries+count,[](const Entry&a,const Entry&b){return a.manifest.name<b.manifest.name;});
}
void list() {
    scan();JsonDocument d;d["chip"]=chip();d["maxApps"]=MaxApps;d["maxBytes"]=max_package_bytes;
    auto apps=d["apps"].to<JsonArray>();
    for(unsigned i=0;i<count;++i){auto a=apps.add<JsonObject>();auto &m=entries[i].manifest;
        a["id"]=m.id;a["name"]=m.name;a["version"]=m.version;a["running"]=native_app::running(m.id.c_str());}
    std::string b;serializeJson(d,b);respond(200,b);
}
void commit_upload() {
    upload.flush();upload.close();Package package;std::string why;auto bytes=read(Stage,package,why);
    if(!bytes){error(422,why);return;}
    auto &id=package.manifest.id;
    if(native_app::running(id.c_str())||ota_busy()){error(409,"Close the running app or finish the OS update first.");return;}
    scan();bool exists=LittleFS.exists(path(id).c_str());
    if(!exists&&count>=MaxApps){error(409,"Installed app limit reached. Remove an app first.");return;}
    // LittleFS rename atomically replaces a regular file: no delete-first gap.
    if(!LittleFS.rename(Stage,path(id).c_str())){error(507,"Could not commit package; existing app retained.");return;}
    dirty=true;respond(200,"{\"ok\":true}");
}
void body(const uint8_t *data,size_t size) {
    if(size>remaining){error(400,"Unexpected bytes after package.");return;}
    if(upload.write(data,size)!=size){error(507,"Storage write failed; existing app retained.");return;}
    remaining-=size;if(!remaining)commit_upload();
}
void parse_headers() {
    auto end=headers.find("\r\n\r\n");if(end==std::string::npos){if(headers.size()>4096)error(431,"Headers too large.");return;}
    if(end>4096){error(431,"Headers too large.");return;}
    auto first=headers.find("\r\n"),space=headers.find(' '),second=headers.find(' ',space+1);
    if(first==std::string::npos||space>first||second>first){error(400,"Bad request.");return;}
    auto method=headers.substr(0,space),route=headers.substr(space+1,second-space-1);
    std::string supplied,origin,host;bool has_length=false;size_t length=0;
    for(size_t pos=first+2;pos<end;){auto finish=headers.find("\r\n",pos),colon=headers.find(':',pos);
        if(colon>=finish){error(400,"Bad header.");return;}
        auto name=headers.substr(pos,colon-pos),value=headers.substr(colon+1,finish-colon-1);
        while(!value.empty()&&value[0]==' ')value.erase(0,1);
        std::transform(name.begin(),name.end(),name.begin(),[](unsigned char c){return std::tolower(c);});
        if(name=="content-length"){
            if(has_length||value.empty()||value.size()>6||value.find_first_not_of("0123456789")!=std::string::npos){error(400,"Invalid length.");return;}
            length=strtoul(value.c_str(),nullptr,10);has_length=true;
        }
        if(name=="transfer-encoding"){error(400,"Chunked requests are not supported.");return;}
        if(name=="x-app-key")supplied=value;if(name=="origin")origin=value;if(name=="host")host=value;
        pos=finish+2;
    }
    if(host!=std::string(WiFi.localIP().toString().c_str())+":8081" || (!origin.empty()&&origin!=address())) {error(403,"Use the device's pairing link.");return;}
    if(method=="GET"&&route=="/"&&length==0){respond(200,AppsPage,"text/html; charset=utf-8");return;}
    if(key.empty()||supplied!=key){error(403,"Pairing expired. Open Apps on the device and scan its link.");return;}
    expires=millis()+600000;
    if(method=="GET"&&route=="/api/apps"&&length==0){list();return;}
    if(method=="DELETE"&&route.rfind("/api/apps/",0)==0&&length==0){
        auto id=route.substr(10);
        if(!valid_app_id(id)){error(400,"Invalid app ID.");return;}
        if(native_app::running(id.c_str())||ota_busy()){error(409,"Close the running app or finish the OS update first.");return;}
        if(!LittleFS.exists(path(id).c_str())){error(404,"App not installed.");return;}
        if(!LittleFS.remove(path(id).c_str())){error(507,"Could not remove app.");return;}
        dirty=true;respond(200,"{\"ok\":true}");return;
    }
    if(method!="POST"||route!="/api/install"){error(405,"Unsupported request.");return;}
    if(!has_length||length<package_header_bytes||length>max_package_bytes){error(413,"Invalid package size.");return;}
    if(ota_busy()){error(409,"Wait for the OS update to finish.");return;}
    if(LittleFS.totalBytes()-LittleFS.usedBytes()<length+8192){error(507,"Not enough space to stage an update.");return;}
    LittleFS.mkdir("/apps");LittleFS.mkdir("/apps/installed");upload=LittleFS.open(Stage,"w");
    if(!upload){error(507,"Cannot stage upload.");return;}
    uploading=true;remaining=length;
    const auto offset=end+4;if(headers.size()>offset)body((const uint8_t*)headers.data()+offset,headers.size()-offset);
    headers.clear();
}
void server_poll() {
    if(!serving)return;
    if(int32_t(millis()-expires)>=0){drop();server.end();serving=false;key.clear();if(notice)lv_label_set_text(notice,"Pairing expired. Close and reopen Apps.");return;}
    if(!peer_active){peer=server.accept();if(!peer)return;peer_active=true;deadline=millis()+5000;started_at=millis();}
    if(int32_t(millis()-deadline)>=0||millis()-started_at>30000){drop();return;}
    if(output.empty()) {
        uint8_t buffer[2048];int available=peer.available();
        if(available){int n=peer.read(buffer,std::min<int>(available,sizeof buffer));if(n>0){deadline=millis()+5000;
            if(uploading)body(buffer,n);else{headers.append((const char*)buffer,n);parse_headers();}}}
        else if(!peer.connected()){drop();return;}
    }
    if(!output.empty()){
        // NetworkClient::write retries synchronously for seconds. Never let a
        // browser that stops reading stall LVGL or the app lifecycle.
        int n=::send(peer.fd(),output.data()+sent,std::min<size_t>(1024,output.size()-sent),MSG_DONTWAIT);
        if(n>0)sent+=n;
        else if(n<0&&errno!=EAGAIN&&errno!=EWOULDBLOCK){drop();return;}
        if(sent==output.size())drop();
    }
}
void show_manager() {
    requested=false;visible=true;native_launcher_release();auto *screen=lv_screen_active();
    lv_obj_set_flex_flow(screen,LV_FLEX_FLOW_COLUMN);lv_obj_set_flex_align(screen,LV_FLEX_ALIGN_CENTER,LV_FLEX_ALIGN_CENTER,LV_FLEX_ALIGN_CENTER);
    auto *title=lv_label_create(screen);lv_label_set_text(title,"Install apps");
    drop();if(serving)server.end();serving=false;key.clear();
    notice=lv_label_create(screen);lv_obj_set_width(notice,lv_pct(95));lv_obj_set_style_text_align(notice,LV_TEXT_ALIGN_CENTER,0);
    if(WiFi.status()==WL_CONNECTED){
        for(int i=0;i<4;++i){char part[9];snprintf(part,sizeof part,"%08x",unsigned(esp_random()));key+=part;}
        server.begin();serving=true;expires=millis()+600000;
        const auto url=address()+"/#"+key;
        auto *qr=lv_qrcode_create(screen);lv_qrcode_set_size(qr,190);lv_qrcode_update(qr,url.data(),url.size());
        lv_label_set_text(notice,(address()+"\nScan to pair your browser.\nInstall trusted development apps only.\nLink expires after 10 minutes idle.").c_str());
        Serial.printf("[NATIVE MANAGER] url=%s\n",url.c_str());
    }else lv_label_set_text(notice,"Connect to Wi-Fi, then reopen Apps.");
    auto *button=lv_button_create(screen);lv_obj_set_size(button,240,58);auto *label=lv_label_create(button);lv_label_set_text(label,"Back to launcher");lv_obj_center(label);
    lv_obj_add_event_cb(button,[](lv_event_t*){close_requested=true;},LV_EVENT_CLICKED,nullptr);
}
struct Icon {lv_image_dsc_t descriptor;char id[33];uint8_t pixels[package_icon_bytes];};
}
const char *chip(){
#if CONFIG_IDF_TARGET_ESP32S3
return "esp32s3";
#else
return "esp32";
#endif
}
std::string path(const std::string &id){return "/apps/installed/"+id+".gapp";}
Buffer read(const char *filename,Package &package,std::string &why) {
    Buffer bytes(nullptr,heap_caps_free);auto f=LittleFS.open(filename,"r");
    if(!f||f.isDirectory()||f.size()<52||f.size()>max_package_bytes){why="Missing or oversized app package.";return bytes;}
    size_t size=f.size();auto *p=(uint8_t*)heap_caps_malloc(size,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    if(!p)p=(uint8_t*)heap_caps_malloc(size,MALLOC_CAP_8BIT);bytes.reset(p);
    if(!p){why="Not enough memory to validate package.";return bytes;}
    if(f.read(p,size)!=size||!validate_package(p,size,chip(),package,why)){if(why.empty())why="Could not read package.";bytes.reset();}
    return bytes;
}
void create_tiles(lv_obj_t *grid) {
    scan();dirty=false;
    lv_obj_add_flag(grid,LV_OBJ_FLAG_SCROLLABLE);lv_obj_set_scroll_dir(grid,LV_DIR_VER);lv_obj_set_scrollbar_mode(grid,LV_SCROLLBAR_MODE_AUTO);
    for(unsigned i=0;i<count;++i){auto &entry=entries[i];
        auto *icon=(Icon*)heap_caps_calloc(1,sizeof(Icon),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
        if(!icon)continue;
        auto f=LittleFS.open(path(entry.manifest.id).c_str());f.seek(entry.icon_offset);
        if(f.read(icon->pixels,package_icon_bytes)!=package_icon_bytes){heap_caps_free(icon);continue;}
        strcpy(icon->id,entry.manifest.id.c_str());icon->descriptor.header.magic=LV_IMAGE_HEADER_MAGIC;
        icon->descriptor.header.cf=LV_COLOR_FORMAT_RGB565;icon->descriptor.header.w=40;icon->descriptor.header.h=40;
        icon->descriptor.header.stride=80;icon->descriptor.data_size=package_icon_bytes;icon->descriptor.data=icon->pixels;
        auto *tile=lv_button_create(grid);lv_obj_set_size(tile,88,112);
        lv_obj_set_style_bg_opa(tile,LV_OPA_TRANSP,0);lv_obj_set_style_shadow_width(tile,0,0);lv_obj_set_style_pad_all(tile,2,0);
        auto *image=lv_image_create(tile);lv_image_set_src(image,&icon->descriptor);lv_obj_align(image,LV_ALIGN_TOP_MID,0,14);
        auto *name=lv_label_create(tile);lv_label_set_text(name,entry.manifest.name.c_str());lv_obj_set_width(name,84);
        lv_obj_set_style_text_align(name,LV_TEXT_ALIGN_CENTER,0);lv_obj_align(name,LV_ALIGN_BOTTOM_MID,0,0);
        lv_obj_add_event_cb(tile,[](lv_event_t *e){native_app::launch_id(((Icon*)lv_event_get_user_data(e))->id);},LV_EVENT_CLICKED,icon);
        lv_obj_add_event_cb(image,[](lv_event_t *e){auto*p=(Icon*)lv_event_get_user_data(e);lv_image_set_src((lv_obj_t*)lv_event_get_target(e),nullptr);lv_image_cache_drop(&p->descriptor);heap_caps_free(p);},LV_EVENT_DELETE,icon);
    }
}
void request_manager(){if(native_launcher_is_active()&&!native_app::busy())requested=true;}
void close_manager(){if(visible)close_requested=true;}
bool managing(){return requested||visible;}
void poll(){
    if(requested)show_manager();
    server_poll();
    if(close_requested){close_requested=false;visible=false;notice=nullptr;native_launcher_restore();}
    else if(dirty&&!native_app::busy()&&native_launcher_is_active())native_launcher_restore();
}
}
#endif
