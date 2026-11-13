#include "experimental/NativeUi.h"
#include "experimental/NativeServices.h"
#include "experimental/NativeSettings.h"
#include "experimental/NativeApp.h"
#include "experimental/NativeSimulator.h"
#include "experimental/NativeStore.h"
#include "services/ConfigStoreService.h"
#include "apps/SettingsApp.h"
#include "AppRegistry.h"
#include <sys/stat.h>
#include "ui/I18n.h"
#include "src/widgets/buttonmatrix/lv_buttonmatrix_private.h"
#include <ArduinoJson.h>
#include <cassert>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>
#include <cstdio>
#include <thread>
#include <chrono>
#define gadget_app_entry stocks_test_entry
namespace source {
#include "../../experiments/native-app/example/stocks.c"
}
#undef gadget_app_entry
namespace services=native_app::services;
namespace settings=native_app::settings;
static uint32_t ticks=1000,mode=0,retry=0;static bool closed=false;
static gadget_http_callback pending=nullptr;static void*pending_user;
static std::string requested;static unsigned requests=0;
static gadget_host_api api;static gadget_services_api service;static gadget_app_lifecycle life;
static uint8_t frame[320*480*3],buffer[320*40*4];
static lv_obj_t *find(lv_obj_t*o,const char*t){if(lv_obj_check_type(o,&lv_label_class)&&!strcmp(lv_label_get_text(o),t))return o;for(unsigned i=0;i<lv_obj_get_child_count(o);++i)if(auto*r=find(lv_obj_get_child(o,i),t))return r;return nullptr;}
static lv_obj_t *type(lv_obj_t*o,const lv_obj_class_t*c){if(lv_obj_check_type(o,c))return o;for(unsigned i=0;i<lv_obj_get_child_count(o);++i)if(auto*r=type(lv_obj_get_child(o,i),c))return r;return nullptr;}
static void step(uint32_t elapsed=250){ticks+=elapsed;source::tick(nullptr);lv_timer_handler();}
static void click(const char*t){auto*l=find(lv_screen_active(),t);assert(l);lv_obj_send_event(lv_obj_get_parent(l),LV_EVENT_CLICKED,nullptr);if(!closed)step();}
static void capture(const char*dir,const char*name){lv_obj_update_layout(lv_screen_active());lv_refr_now(nullptr);std::ofstream f(std::string(dir)+"/stocks-package-"+name+".ppm",std::ios::binary);f<<"P6\n320 480\n255\n";f.write(reinterpret_cast<char*>(frame),sizeof(frame));}
static std::string quote(const char*symbol="AAPL"){
 JsonDocument d;auto q=d["chart"]["result"].to<JsonArray>().add<JsonObject>();auto m=q["meta"].to<JsonObject>();
 m["symbol"]=symbol;m["longName"]="Apple";m["currency"]="USD";m["regularMarketPrice"]=230;m["regularMarketTime"]=259300;m["gmtoffset"]=0;m["chartPreviousClose"]=100;m["exchangeName"]="NASDAQ";
 auto times=q["timestamp"].to<JsonArray>();for(int n:{86401,172801,259201})times.add(n);
 auto values=q["indicators"]["quote"].to<JsonArray>().add<JsonObject>()["close"].to<JsonArray>();for(int n:{225,227,230})values.add(n);
 std::string s;serializeJson(d,s);return s;
}
static void respond(int status,const std::string&body){assert(pending);auto cb=pending;pending=nullptr;cb(status,body.data(),body.size(),pending_user);step();}
static void launch(bool settings_mode=false){
 closed=false;pending=nullptr;mode=settings_mode?GADGET_LAUNCH_SETTINGS:GADGET_LAUNCH_NORMAL;
 auto *old=lv_screen_active();lv_screen_load(lv_obj_create(nullptr));lv_obj_delete(old);
 settings::begin("stocks-preview");services::begin("stocks-preview");native_app::ui::begin(lv_screen_active(),[](gadget_callback cb,void*u){if(!closed)cb(u);});
 service={};service.struct_size=sizeof(service);service.environment=services::environment;service.format_time=services::format_time;service.json_open=services::json_open;service.json_get=services::json_get;service.json_count=services::json_count;service.json_close=services::json_close;service.json_real=services::json_real;service.format_number=services::format_number;service.monotonic_ms=[](){return ticks;};service.math=&services::math_api;
 service.http_retry_seconds=[](){return retry;};service.http_get=[](const char*url,uint32_t cap,gadget_http_callback cb,void*u)->int32_t{assert(!pending&&cap==65536);pending=cb;pending_user=u;requested=url;++requests;return 0;};
 api={};api.struct_size=sizeof(api);api.major=1;api.minor=9;api.ui=&native_app::ui::api;api.services=&service;api.setting_get=settings::get;api.setting_set=settings::set;api.launch_mode=[](){return mode;};api.request_close=[](){closed=true;native_app::ui::suspend();};api.timer_create=[](uint32_t,gadget_callback,void*)->gadget_handle{return (void*)1;};
 life={sizeof(life)};char*args[]={reinterpret_cast<char*>(&api),reinterpret_cast<char*>(&life)};assert(source::stocks_test_entry(2,args)==0);
}
static void close(){life.shutdown();native_app::ui::end();settings::end();services::end();pending=nullptr;}
static void key(const char*text){auto *kb=type(lv_screen_active(),&lv_keyboard_class);assert(kb);unsigned id=0;while(auto*t=lv_buttonmatrix_get_button_text(kb,id)){if(!strcmp(t,text))break;++id;}assert(lv_buttonmatrix_get_button_text(kb,id));lv_obj_update_layout(kb);
 auto area=reinterpret_cast<lv_buttonmatrix_t*>(kb)->button_areas[id];lv_area_t bounds;lv_obj_get_coords(kb,&bounds);static lv_indev_data_t data;
 auto*pointer=lv_indev_create();lv_indev_set_type(pointer,LV_INDEV_TYPE_POINTER);lv_indev_set_read_cb(pointer,[](lv_indev_t*,lv_indev_data_t*out){*out=data;});
 data.point={bounds.x1+(area.x1+area.x2)/2,bounds.y1+(area.y1+area.y2)/2};data.state=LV_INDEV_STATE_PRESSED;ticks+=30;lv_indev_read(pointer);data.state=LV_INDEV_STATE_RELEASED;ticks+=30;lv_indev_read(pointer);lv_indev_delete(pointer);
}
int main(int argc,char**argv){assert(argc==2);auto&cfg=ConfigStoreService::instance();assert(cfg.begin());assert(cfg.load("stocks"));cfg.set("symbols",std::string("AAPL\nBTC-USD"));cfg.set("provider",std::string("yahoo"));assert(cfg.save());
 lv_init();lv_tick_set_cb([](){return ticks;});auto*d=lv_display_create(320,480);lv_display_set_color_format(d,LV_COLOR_FORMAT_XRGB8888);lv_display_set_buffers(d,buffer,nullptr,sizeof(buffer),LV_DISPLAY_RENDER_MODE_PARTIAL);
 lv_display_set_flush_cb(d,[](lv_display_t*d,const lv_area_t*a,uint8_t*p){for(int y=a->y1;y<=a->y2;++y)for(int x=a->x1;x<=a->x2;++x){int src=((y-a->y1)*(a->x2-a->x1+1)+x-a->x1)*4,dst=(y*320+x)*3;frame[dst]=p[src+2];frame[dst+1]=p[src+1];frame[dst+2]=p[src];}lv_display_flush_ready(d);});i18n::init();i18n::apply_fonts(d);lv_sysmon_hide_performance(d);
 launch();assert(source::count==2);assert(cfg.load("stocks")&&cfg.get_bool("package_watchlist_migrated",false));assert(cfg.get("symbols","")=="AAPL\nBTC-USD");
 step();assert(requested.find("/AAPL?range=5d")!=std::string::npos);respond(200,quote());assert(source::stocks[0].valid&&source::stocks[0].previous==227);capture(argv[1],"watchlist");
 // Real pointer input must not rebuild the root under a finger or scrolling view.
 static lv_indev_data_t pointer_data;auto *pointer=lv_indev_create();lv_indev_set_type(pointer,LV_INDEV_TYPE_POINTER);
 lv_indev_set_read_cb(pointer,[](lv_indev_t*,lv_indev_data_t*out){*out=pointer_data;});
 lv_obj_update_layout(lv_screen_active());pointer_data.point={160,240};pointer_data.state=LV_INDEV_STATE_PRESSED;lv_indev_read(pointer);
 auto original_body=source::body;source::dirty=1;step(300);assert(source::dirty&&source::body==original_body);
 for(int y=250;y<=470;y+=10){pointer_data.point.y=y;ticks+=20;lv_indev_read(pointer);source::tick(nullptr);lv_timer_handler();}
 pointer_data.state=LV_INDEV_STATE_RELEASED;lv_indev_read(pointer);for(int i=0;i<50;++i)step(20);lv_indev_delete(pointer);
 assert(source::view==0&&source::notice[0]);
 click("AAPL");capture(argv[1],"detail");assert(find(lv_screen_active(),"230.00 USD"));assert(find(lv_screen_active(),"+1.32%"));
 click("1Y");step(5000);assert(requested.find("range=1y")!=std::string::npos);respond(200,quote());assert(source::stocks[0].period==2);
 // Invalid data never replaces a cached quote; Retry-After blocks manual retries.
 source::stocks[0].due=ticks;step(5000);retry=120;respond(429,"");assert(source::stocks[0].valid&&source::stocks[0].due>=ticks+119000);unsigned before=requests;source::refresh(nullptr);step(30000);assert(requests==before);retry=0;
 // Parser validates symbols, chart ordering and missing previous close semantics.
 source::Stock parsed{};std::strcpy(parsed.symbol,"AAPL");auto valid=quote();assert(source::parse_quote(valid.data(),valid.size(),&parsed));
 auto wrong=quote("MSFT");assert(!source::parse_quote(wrong.data(),wrong.size(),&parsed));
 JsonDocument broken;deserializeJson(broken,valid);broken["chart"]["result"][0]["timestamp"][1]=86401;std::string bad;serializeJson(broken,bad);assert(!source::parse_quote(bad.data(),bad.size(),&parsed));
 // All keyboard layouts insert Enter into the actual SDK multiline field.
 click("Edit");auto*ta=type(lv_screen_active(),&lv_textarea_class);assert(ta);lv_textarea_set_text(ta,"btc-usd");key(LV_SYMBOL_NEW_LINE);key("ABC");key(LV_SYMBOL_NEW_LINE);key("1#");key(LV_SYMBOL_NEW_LINE);assert(std::string(lv_textarea_get_text(ta))=="btc-usd\n\n\n");
 lv_textarea_set_text(ta,"aapl\nAAPL");click("Save");assert(source::view==2&&find(lv_screen_active(),"Duplicate symbol"));
 assert(api.ui->input_config(source::input,256,1)==-1);char tiny[2];assert(api.ui->input_get(source::input,tiny,sizeof(tiny))==-1);auto old_input=source::input;
 lv_textarea_set_text(ta,"btc-usd\nSOL-USD");capture(argv[1],"editor");click("Save");assert(source::view==0&&source::count==2&&std::string(source::stocks[0].symbol)=="BTC-USD");assert(api.ui->input_get(old_input,tiny,sizeof(tiny))==-1);close();
 // Do not re-import modified legacy data over package settings; external Save closes.
 assert(cfg.load("stocks"));cfg.set("symbols",std::string("MSFT"));assert(cfg.save());launch(true);assert(source::count==2&&std::string(source::stocks[0].symbol)=="BTC-USD");ta=type(lv_screen_active(),&lv_textarea_class);lv_textarea_set_text(ta,"");click("Save");assert(closed);close();launch(true);assert(source::count==0);click("Cancel");assert(closed);close();
 // A late response after editing cannot populate an unrelated new symbol.
 settings::begin("stocks-preview");assert(settings::set("symbols","AAPL")==0);settings::end();launch();step();auto late=pending;auto*u=pending_user;pending=nullptr;click("Edit");ta=type(lv_screen_active(),&lv_textarea_class);lv_textarea_set_text(ta,"MSFT");click("Save");late(200,valid.data(),valid.size(),u);assert(!source::stocks[0].valid);close();
 // Cancellation gates callbacks before teardown, repeated sessions reclaim widgets.
 for(int i=0;i<30;++i){launch();step();auto cb=pending;auto*u=pending_user;source::shutdown_app();cb(200,valid.data(),valid.size(),u);assert(!source::stocks[0].valid);close();}
 // Uninstall removes app-owned settings; the legacy marker prevents resurrection.
 std::remove(".pio/native-app-settings/stocks-preview.json");launch(true);assert(source::count==5&&std::string(source::stocks[0].symbol)=="AAPL");close();

 // Installed simulator adapter: real package source, async fixture transport,
 // Settings integration, reinstall persistence and uninstall cleanup.
 for(int i=0;i<AppRegistry::instance().count();++i)assert(std::string(AppRegistry::instance().apps()[i].id)!="stocks");
 mkdir("fixtures",0700);{std::ofstream f("fixtures/AAPL.json");f<<valid;}setenv("GADGET_OS_STOCK_FIXTURE_DIR","fixtures",1);
 settings::begin("stocks-preview");assert(settings::set("symbols","AAPL")==0);settings::end();
 std::string install_error;native_app::catalog::Item stock_item;
 for(const auto&item:native_app::simulator::catalog())if(item.id=="stocks")stock_item=item;
 assert(stock_item.id=="stocks"&&native_app::simulator::install(stock_item,install_error));
 native_launcher_restore();native_app::launch_id("stocks");native_app::poll();
 for(int i=0;i<100&&!find(lv_screen_active(),"230.00 USD");++i){ticks+=300;lv_timer_handler();native_app::poll();std::this_thread::sleep_for(std::chrono::milliseconds(2));}
 assert(find(lv_screen_active(),"230.00 USD"));click(LV_SYMBOL_LEFT);native_app::poll();assert(native_launcher_is_active());
 assert(native_app::simulator::install(stock_item,install_error));settings_launch_with_route("settings_apps");
 assert(native_app::launch_settings_id("stocks"));native_app::poll();
 ta=type(lv_screen_active(),&lv_textarea_class);assert(ta&&std::string(lv_textarea_get_text(ta))=="AAPL");
 lv_textarea_set_text(ta,"BTC-USD");click("Save");native_app::poll();assert(settings_apps_active());
 settings::begin("stocks-preview");char saved[256];assert(settings::get("symbols",saved,sizeof(saved))==7&&std::string(saved)=="BTC-USD");settings::end();
 assert(native_app::store::uninstall("stocks",install_error));assert(!std::ifstream(".pio/native-app-settings/stocks-preview.json"));
 assert(cfg.load("stocks")&&cfg.get("symbols","")=="MSFT"&&cfg.get_bool("package_watchlist_migrated",false));
 std::cout<<"PASS: package source, Yahoo parsing/history, Retry-After, generation fencing, input/Enter, settings Back, migration and 30 cleanup cycles\n";
}
