#include "experimental/NativeUi.h"
#include "experimental/NativeServices.h"
#include "experimental/NativeSettings.h"
#include "experimental/NativeSetup.h"
#include "experimental/NativeSimulator.h"
#include "experimental/NativeStore.h"
#include "experimental/NativeApp.h"
#include "services/ConfigStoreService.h"
#include "services/FileSystemService.h"
#include "apps/SettingsApp.h"
#include "ui/I18n.h"
#include <ArduinoJson.h>
#include <cassert>
#include <cstring>
#include <string>
#include <fstream>
#include <iostream>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#define gadget_app_entry transit_entry
namespace transit {
#include "../../experiments/native-app/example/transit.c"
}
#undef gadget_app_entry
#define gadget_app_entry planes_entry
namespace planes {
#include "../../experiments/native-app/example/planes.c"
}
#undef gadget_app_entry
#define gadget_app_entry home_entry
namespace home {
#include "../../experiments/native-app/example/home_assistant.c"
}
#undef gadget_app_entry
namespace services=native_app::services;namespace settings=native_app::settings;
static uint32_t ticks=100000,mode=0,retry=0;static bool closed=false;
static gadget_host_api api;static gadget_services_api svc;static gadget_app_lifecycle life;
static gadget_http_callback pending=nullptr;static void*pending_user;static std::string url,method,payload,bearer;static unsigned requests=0;
static uint8_t pixels[320*480*3],draw_buffer[320*40*4];
static const char*which;static void(*tick)(void*);
static int32_t http(const gadget_http_request*r,gadget_http_callback cb,void*u){assert(!pending);assert(r->response_limit<=262144);pending=cb;pending_user=u;url=r->url;method=r->method;payload=r->body;bearer=r->bearer;++requests;return 0;}
static void step(unsigned ms=250){ticks+=ms;tick(nullptr);lv_timer_handler();}
static void reply(int status,const std::string&json){assert(pending);auto cb=pending;pending=nullptr;cb(status,json.data(),json.size(),pending_user);step(1);}
static void launch(const char*id,int(*entry)(int,char**),void(*timer)(void*),bool external=false){which=id;tick=timer;closed=false;mode=external?GADGET_LAUNCH_SETTINGS:GADGET_LAUNCH_NORMAL;pending=nullptr;
 auto*old=lv_screen_active();lv_screen_load(lv_obj_create(nullptr));lv_obj_delete(old);settings::begin(id);services::begin(id);native_app::ui::begin(lv_screen_active(),[](gadget_callback cb,void*u){if(!closed)cb(u);});
 svc={};svc.struct_size=sizeof svc;svc.environment=[](gadget_environment*e)->int32_t{*e={sizeof(*e)};e->epoch=1800000000;e->online=e->clock_ready=e->has_location=1;e->latitude_e6=47376000;e->longitude_e6=8541000;return 0;};svc.parse_time=services::parse_time;svc.format_time=services::format_time;svc.json_open=services::json_open;svc.json_get=services::json_get;svc.json_count=services::json_count;svc.json_close=services::json_close;svc.json_number=services::json_number;svc.json_real=services::json_real;svc.format_number=services::format_number;svc.monotonic_ms=[](){return ticks;};svc.math=&services::math_api;svc.cache_read=services::cache_read;svc.cache_write=services::cache_write;svc.config_read=settings::config_read;svc.config_write=settings::config_write;svc.http_request=http;svc.http_get=[](const char*url,uint32_t cap,gadget_http_callback cb,void*u)->int32_t{gadget_http_request r{sizeof r,url,"GET","","","",cap};return http(&r,cb,u);};svc.http_retry_seconds=[](){return retry;};svc.setup_start=native_app::setup::start;svc.setup_stop=native_app::setup::stop;svc.setup_active=native_app::setup::is_active;
 api={};api.struct_size=sizeof api;api.major=1;api.minor=11;api.ui=&native_app::ui::api;api.services=&svc;api.timer_create=[](uint32_t,gadget_callback,void*)->gadget_handle{return (void*)1;};api.request_close=[](){closed=true;native_app::setup::stop();native_app::ui::suspend();};api.launch_mode=[](){return mode;};api.setting_get=settings::get;api.setting_set=settings::set;
 life={sizeof life};char*args[]={(char*)&api,(char*)&life};assert(entry(2,args)==0);
}
static lv_obj_t*find_label(lv_obj_t*o,const char*t){if(lv_obj_check_type(o,&lv_label_class)&&!strcmp(lv_label_get_text(o),t))return o;for(unsigned i=0;i<lv_obj_get_child_count(o);++i)if(auto*r=find_label(lv_obj_get_child(o,i),t))return r;return nullptr;}
static lv_obj_t*find_textarea(){for(unsigned i=0;i<lv_obj_get_child_count(lv_screen_active());++i){auto*o=lv_obj_get_child(lv_screen_active(),i);if(lv_obj_check_type(o,&lv_textarea_class))return o;}return nullptr;}
static void close_app(){life.shutdown();native_app::setup::stop();native_app::ui::end();settings::end();services::end();pending=nullptr;}
static void tap_xy(int x,int y){lv_obj_update_layout(lv_screen_active());static lv_indev_data_t data;auto*pointer=lv_indev_create();lv_indev_set_type(pointer,LV_INDEV_TYPE_POINTER);lv_indev_set_read_cb(pointer,[](lv_indev_t*,lv_indev_data_t*out){*out=data;});data.point={x,y};data.state=LV_INDEV_STATE_PRESSED;ticks+=30;lv_indev_read(pointer);data.state=LV_INDEV_STATE_RELEASED;ticks+=30;lv_indev_read(pointer);lv_indev_delete(pointer);step(1);}
static void capture(const char*dir,const char*name){lv_obj_update_layout(lv_screen_active());lv_refr_now(nullptr);std::ofstream f(std::string(dir)+"/phase3-"+name+".ppm",std::ios::binary);f<<"P6\n320 480\n255\n";f.write((char*)pixels,sizeof pixels);}
static std::string setup_call(const std::string&request){std::string address=home::setup_url;auto colon=address.rfind(':');int port=atoi(address.c_str()+colon+1);int fd=socket(AF_INET,SOCK_STREAM,0);sockaddr_in a{};a.sin_family=AF_INET;a.sin_port=htons(port);a.sin_addr.s_addr=htonl(INADDR_LOOPBACK);assert(connect(fd,(sockaddr*)&a,sizeof a)==0);assert(write(fd,request.data(),request.size())==(ssize_t)request.size());fcntl(fd,F_SETFL,O_NONBLOCK);std::string result;for(int i=0;i<300;++i){native_app::setup::poll();char b[4096];int n=read(fd,b,sizeof b);if(n>0)result.append(b,n);else if(n==0)break;ticks+=1;}::close(fd);return result;}
int main(int argc,char**argv){assert(argc==2);setenv("GADGET_OS_SETUP_EPHEMERAL_PORT","1",1);lv_init();lv_tick_set_cb([](){return ticks;});auto*d=lv_display_create(320,480);lv_display_set_color_format(d,LV_COLOR_FORMAT_XRGB8888);lv_display_set_buffers(d,draw_buffer,nullptr,sizeof draw_buffer,LV_DISPLAY_RENDER_MODE_PARTIAL);lv_display_set_flush_cb(d,[](lv_display_t*d,const lv_area_t*a,uint8_t*p){for(int y=a->y1;y<=a->y2;++y)for(int x=a->x1;x<=a->x2;++x){int src=((y-a->y1)*(a->x2-a->x1+1)+x-a->x1)*4,dst=(y*320+x)*3;pixels[dst]=p[src+2];pixels[dst+1]=p[src+1];pixels[dst+2]=p[src];}lv_display_flush_ready(d);});i18n::init();i18n::apply_fonts(d);lv_sysmon_hide_performance(d);
 auto&cfg=ConfigStoreService::instance();assert(cfg.begin());assert(cfg.load("transit"));std::string stops=R"([{"id":"ch:8503000","name":"Zurich HB","area":"CH"}])";cfg.set("provider",std::string("transitous"));cfg.set("favourites",stops);assert(cfg.save());
 launch("transit-preview",transit::transit_entry,transit::tick);assert(transit::count==1);assert(cfg.load("transit")&&cfg.get("favourites","")==stops&&cfg.get_bool("package_config_migrated",false));step();assert(url.find("ch%3A8503000")!=std::string::npos);
 std::string board=R"({"stopTimes":[{"displayName":"IC 1","headsign":"Bern","tripId":"trip/one","realTime":true,"place":{"departure":"2027-01-15T08:04:00Z","scheduledDeparture":"2027-01-15T08:00:00Z","track":"31"}},{"displayName":"S 2","headsign":"Airport","tripId":"two","cancelled":true,"place":{"scheduledDeparture":"2027-01-15T08:00:00Z"}}]})";
 reply(200,board);assert(transit::departure_count==2&&transit::departures[0].cancelled);capture(argv[1],"transit-board");
 tap_xy(35,215);assert(transit::view==3);step(6000);assert(url.find("trip%2Fone")!=std::string::npos);reply(200,R"({"legs":[{"from":{"name":"Zurich HB","departure":"2027-01-15T08:04:00Z"},"intermediateStops":[{"name":"Olten","arrival":"2027-01-15T08:40:00Z"}],"to":{"name":"Bern","arrival":"2027-01-15T09:00:00Z"}}]})");assert(transit::call_count==3);assert(transit::board_valid);auto saved_departures=transit::departure_count;transit::back(nullptr);assert(transit::departure_count==saved_departures&&!transit::pending);transit::details((void*)1);step(6000);reply(200,R"({"legs":[]})");capture(argv[1],"transit-details");
 transit::settings(nullptr);step();transit::search_screen(nullptr);step();lv_obj_t*ta=nullptr;for(unsigned i=0;i<lv_obj_get_child_count(lv_screen_active());++i){auto*o=lv_obj_get_child(lv_screen_active(),i);if(lv_obj_check_type(o,&lv_textarea_class))ta=o;}assert(ta);assert(lv_obj_has_state(ta,LV_STATE_FOCUSED));capture(argv[1],"transit-search");lv_textarea_set_text(ta,"Bern");transit::search(nullptr);step(6000);assert(url.find("text=Bern")!=std::string::npos);reply(200,R"([{"type":"STOP","id":"ch:8507000","name":"Bern","country":"CH"}])");assert(transit::found_count==1);capture(argv[1],"transit-results");transit::add(nullptr);step();assert(transit::count==2);transit::settings(nullptr);step();capture(argv[1],"transit-settings");transit::ask_remove(nullptr);step();assert(transit::count==2&&transit::view==5);transit::back(nullptr);step();assert(transit::count==2&&transit::view==1);transit::info(nullptr);step();assert(find_label(lv_screen_active(),"Transitous / MOTIS"));transit::board();step(6000);assert(pending);auto before=transit::departure_count;transit::settings(nullptr);reply(200,board);assert(transit::departure_count==before);transit::request_kind=0;std::string early=R"({"stopTimes":[{"headsign":"Geneve","place":{"departure":"2027-01-15T08:04:00Z","scheduledDeparture":"2027-01-15T08:06:00Z"},"realTime":true,"routeColor":"000000"}]})";assert(transit::parse(early.c_str(),early.size()));assert(std::string(transit::departures[0].line)=="Transit"&&transit::departures[0].color==0);transit::details(nullptr);assert(transit::view==3&&std::string(transit::notice)=="No route details available");transit::render();assert(find_label(lv_screen_active(),"Live prediction -2 min"));close_app();
 launch("transit-preview",transit::transit_entry,transit::tick,true);assert(transit::count==2);transit::back(nullptr);assert(closed);close_app();settings::erase_config("transit-preview");launch("transit-preview",transit::transit_entry,transit::tick);assert(transit::count==0);close_app();
 launch("planes-shared",planes::planes_entry,planes::tick);assert(!planes::override_location);planes::airports.count=1;planes::cp(planes::airports.airports[0].code,"ZRH",9);planes::airports.airports[0].lat=47.458;planes::airports.airports[0].lon=8.555;assert(svc.cache_write(&planes::airports,sizeof planes::airports)>=0);close_app();launch("planes-shared",planes::planes_entry,planes::tick);assert(!planes::override_location&&planes::airports.count==1);close_app();
 assert(cfg.load("planes"));cfg.set("center_latitude",std::string("47.376"));cfg.set("center_longitude",std::string("8.541"));cfg.set("range_km",std::string("25"));cfg.set("metric",false);cfg.set("hide_ground",true);assert(cfg.save());
 auto&fs=FileSystemService::instance();assert(fs.begin());fs.fs().mkdir_p("/cache");assert(fs.fs().write("/cache/plane-airports.json",R"({"version":1,"lat":47.376,"lon":8.541,"range":25,"airports":[{"icao":"LSZH","code":"ZRH","lat":47.458,"lon":8.555}]})"));
 launch("planes-preview",planes::planes_entry,planes::tick);assert(planes::override_location&&!planes::metric&&planes::hide_ground);assert(planes::airports.count==1&&std::string(planes::airports.airports[0].code)=="ZRH");step();assert(url.find("adsb.lol")!=std::string::npos);reply(500,"");step(6000);assert(url.find("airplanes.live")!=std::string::npos);
 std::string aircraft=R"({"ac":[{"hex":"4b1805","flight":"SWR12A ","lat":47.48,"lon":8.62,"alt_baro":12300,"gs":310,"track":245,"baro_rate":-640,"r":"HB-JBA","t":"BCS3"},{"hex":"ground","flight":"GROUND","lat":47.37,"lon":8.54,"alt_baro":"ground"}]})";reply(200,aircraft);assert(planes::count==1&&std::string(planes::planes[0].callsign)=="SWR12A");int x,y;assert(planes::position(planes::planes[0].lat,planes::planes[0].lon,&x,&y));tap_xy(x+10,y+74);assert(std::string(planes::selected_id)=="4b1805"&&planes::view==0);step(6000);assert(url.find("callsign/SWR12A")!=std::string::npos);reply(200,R"({"response":{"flightroute":{"origin":{"iata_code":"ZRH","name":"Zurich Airport","latitude":47.458,"longitude":8.555},"destination":{"iata_code":"JFK","name":"John F Kennedy","latitude":40.64,"longitude":-73.78}}}})");assert(std::string(planes::route)=="ZRH -> JFK"&&planes::airports.count==1);capture(argv[1],"planes-radar");tap_xy(100,370);assert(planes::view==2);capture(argv[1],"planes-details");planes::ui->scroll_y(planes::body,100);assert(planes::ui->scroll_y(planes::body,-1)>0);planes::render();assert(planes::ui->scroll_y(planes::body,-1)>0);planes::back(nullptr);planes::zoom(nullptr);step();assert(planes::range_open);capture(argv[1],"planes-ranges");planes::back(nullptr);assert(!planes::range_open&&planes::view==0);planes::settings(nullptr);step();capture(argv[1],"planes-settings");
 // Location helpers update only the draft, and stale/cancelled replies cannot overwrite it.
 planes::location(nullptr);step();capture(argv[1],"planes-location");
 const double saved_lat=planes::latitude,saved_lon=planes::longitude;
 planes::guess_location(nullptr);step();assert(url.find("https://ipwho.is/")==0);assert(planes::busy);
 planes::guess_location(nullptr);assert(!planes::guess_pending); // repeated tap does not queue a second lookup
 reply(200,R"({"success":true,"latitude":46.948,"longitude":7.447,"city":"Bern","region":"Bern"})");
 assert(std::abs(planes::edit_lat-46.948)<0.00001&&std::abs(planes::edit_lon-7.447)<0.00001&&planes::edit_override);
 assert(planes::latitude==saved_lat&&planes::longitude==saved_lon);capture(argv[1],"planes-location-found");
 planes::guess_location(nullptr);step();reply(200,R"({"success":true,"latitude":999,"longitude":7})");
 assert(std::abs(planes::edit_lat-46.948)<0.00001&&strstr(planes::location_notice,"failed"));
 planes::guess_location(nullptr);step();reply(503,"");assert(std::abs(planes::edit_lat-46.948)<0.00001);
 planes::guess_location(nullptr);step();planes::choice((void*)4);reply(200,R"({"success":true,"latitude":40,"longitude":-70})");assert(!planes::edit_override&&std::abs(planes::edit_lat-46.948)<0.00001);
 planes::guess_location(nullptr);step();planes::back(nullptr);reply(200,R"({"success":true,"latitude":41,"longitude":-71})");assert(planes::view==1&&std::abs(planes::edit_lat-46.948)<0.00001);
 planes::location(nullptr);planes::choice((void*)5);planes::edit(nullptr);step();
 // Check every Planes numeric editor, including an empty value and after
 // enough idle time for the old theme cursor to have blinked off.
 for(unsigned field=0;field<3;++field){
  planes::edit((void*)(uintptr_t)field);step();auto *ta=find_textarea();assert(ta);
  assert(lv_obj_has_state(ta,LV_STATE_FOCUSED));
  assert(lv_obj_get_style_border_width(ta,LV_PART_CURSOR)==2);
  assert(lv_obj_get_style_anim_duration(ta,LV_PART_CURSOR)==0);
  lv_textarea_set_text(ta,"");step(500);
  assert(lv_obj_get_style_border_opa(ta,LV_PART_CURSOR)==LV_OPA_COVER);
  if(field==0)capture(argv[1],"planes-latitude-cursor");
 }
 planes::edit(nullptr);step();
 auto *coordinate=find_textarea();assert(lv_obj_check_type(coordinate,&lv_textarea_class));
 lv_textarea_set_text(coordinate,"91");planes::edit_save(nullptr);step();assert(planes::view==3&&find_label(lv_screen_active(),"Enter a valid number"));
 coordinate=find_textarea();lv_textarea_set_text(coordinate," 47,376 ");planes::edit_save(nullptr);step();assert(planes::view==5&&std::abs(planes::edit_lat-47.376)<0.00001);
 planes::back(nullptr);planes::range_choice((void*)50);assert(planes::edit_range==50&&planes::range_km==25);
 planes::choice((void*)0);planes::choice((void*)2);assert(planes::edit_metric&&!planes::edit_hide);
 planes::edit_lat=saved_lat;planes::edit_lon=saved_lon;planes::edit_metric=planes::metric;planes::edit_hide=planes::hide_ground;planes::edit_override=planes::override_location;
 planes::edit_range=51;planes::back(nullptr);assert(planes::view==4);planes::keep_editing(nullptr);assert(planes::view==1);
 assert(mkdir(".pio/native-app-settings/planes-preview.config.json.tmp",0700)==0);planes::save(nullptr);assert(planes::range_km==25&&planes::view==1);assert(rmdir(".pio/native-app-settings/planes-preview.config.json.tmp")==0);planes::edit_range=25;
 planes::edit_override=0;planes::save(nullptr);assert(!planes::override_location);close_app();
 launch("planes-preview",planes::planes_entry,planes::tick,true);assert(!planes::override_location);planes::back(nullptr);assert(closed);close_app();
 // Feature parity: collect every aircraft's history, never put ground targets
 // on the nearby radar, preserve histories on selection/zoom, clip at the rim.
 launch("planes-audit",planes::planes_entry,planes::tick);planes::hide_ground=0;
 auto aircraft_sample=[](double shift){JsonDocument d;auto a=d["ac"].to<JsonArray>();for(int i=0;i<2;++i){auto p=a.add<JsonObject>();p["hex"]=i?"bbbbbb":"aaaaaa";p["flight"]=i?"BAW2":"SWR1";p["lat"]=47.38+shift+i*.01;p["lon"]=8.55;p["alt_baro"]=5000+i*5000;p["track"]=45;}auto g=a.add<JsonObject>();g["hex"]="ground";g["lat"]=47.376;g["lon"]=8.541;g["alt_baro"]="ground";std::string raw;serializeJson(d,raw);assert(planes::decode(raw.c_str(),raw.size()));};
 aircraft_sample(0);aircraft_sample(.01);aircraft_sample(.02);assert(planes::count==2&&planes::trail_count==3);
 planes::select_plane((void*)1);assert(planes::trail_count==3);planes::select_plane(nullptr);assert(planes::trail_count==3);
 aircraft_sample(.02);assert(planes::trail_count==3); // unchanged location is not a zero-length trail sample
 planes::radar_range((void*)5);aircraft_sample(.02);assert(planes::trail_count==3);
 int ax,ay,bx,by;assert(planes::trail_segment({47.376,8.40,1000},{47.376,8.68,2000},&ax,&ay,&bx,&by));assert(ax>=38&&bx<=262);
 planes::render();capture(argv[1],"planes-trail");
 // Any aircraft can teach landmarks; route cache is selected by callsign, not
 // by whichever plane happens to be selected when the HTTP response arrives.
 planes::route_slot=0;planes::cp(planes::route_call,"BAW2",sizeof planes::route_call);
 std::string airport_route=R"({"response":{"flightroute":{"origin":{"iata_code":"AAA","icao_code":"TEST","municipality":"Geneve","latitude":47.38,"longitude":8.55},"destination":{"iata_code":"BBB","name":"Destination","latitude":40,"longitude":-70}}}})";
 planes::route_parse(airport_route.c_str(),airport_route.size());assert(planes::airports.count==1);planes::select_plane((void*)1);assert(std::string(planes::route)=="AAA -> BBB"&&std::string(planes::origin)=="Geneve");
 auto route_expiry=planes::routes[0].expires;assert(route_expiry==ticks+21600000);planes::select_plane(nullptr);assert(planes::route[0]==0);planes::select_plane((void*)1);assert(std::string(planes::route)=="AAA -> BBB");
 planes::details(nullptr);std::string empty=R"({"ac":[]})";assert(planes::decode(empty.c_str(),empty.size()));planes::render();assert(find_label(lv_screen_active(),"Aircraft no longer in view"));
 close_app();
 // Scope and persistence: same view reloads landmarks, changing center invalidates them.
 launch("planes-audit",planes::planes_entry,planes::tick); // default range differs from the 5 km snapshot
 assert(planes::airports.count==0);close_app();
 assert(cfg.load("home-assistant"));std::string configuration=R"({"url":"http://homeassistant.local:8123","token":"private-test-token","ca":"","pages":[{"name":"Living room","tiles":[{"id":"light.desk","name":"Desk light","icon":"light"},{"id":"sensor.temperature","name":"Temperature","icon":"temperature"}]}]})";cfg.set("configuration",configuration);assert(cfg.save());
 launch("home-assistant-preview",home::home_entry,home::tick);assert(home::config.count==1);step();assert(url.find("/api/states/light.desk")!=std::string::npos&&bearer=="private-test-token");std::string light=R"({"entity_id":"light.desk","state":"off","attributes":{"friendly_name":"Desk light","brightness":100,"color_temp_kelvin":3000,"supported_color_modes":["rgb","color_temp"]}})";reply(200,light);assert(home::entities[0].valid&&home::entities[0].color);step(500);reply(200,R"({"entity_id":"sensor.temperature","state":"21.3","attributes":{"unit_of_measurement":"C"}})");capture(argv[1],"home-dashboard");assert(find_label(lv_screen_active(),"Connected - refreshes every 5s"));auto tile_handle=home::tile_rows[0];home::dirty=1;step(1);assert(home::tile_rows[0]==tile_handle);char home_status[32768];assert(home::setup_request("GET","/api/status","",home_status,sizeof home_status,nullptr)==200&&strstr(home_status,"\"connected\":true"));home::tap((void*)28);step();capture(argv[1],"home-details");home::action(0,1,0);step(500);assert(method=="POST"&&url.find("/api/services/light/turn_on")!=std::string::npos);reply(200,"[]");assert(home::entities[0].action&&home::entities[0].accepted);step(500);assert(url.find("/api/states/light.desk")!=std::string::npos);reply(200,light);assert(home::entities[0].action);step(500);reply(200,R"({"entity_id":"light.desk","state":"on","attributes":{"brightness":100,"supported_color_modes":["rgb","color_temp"]}})");assert(!home::entities[0].action);
 home::setup(nullptr);step();capture(argv[1],"home-setup");assert(native_app::setup::is_active());auto result=setup_call("GET /api/config HTTP/1.1\r\nHost: localhost\r\n\r\n");assert(result.find("403")!=std::string::npos);std::string key=std::string(home::setup_url).substr(std::string(home::setup_url).find('#')+1);result=setup_call("GET /api/config HTTP/1.1\r\nHost: localhost\r\nX-Setup-Key: "+key+"\r\n\r\n");assert(result.find("200")!=std::string::npos&&result.find("private-test-token")==std::string::npos&&result.find("has_token")!=std::string::npos);
 char response[32768];
 std::string invalid=configuration;invalid.replace(invalid.find("private-test-token"),18,"bad\\r\\nHeader");assert(home::setup_request("POST","/api/config",invalid.c_str(),response,sizeof response,nullptr)==400);assert(std::string(home::config.token)=="private-test-token");
 std::string retain=configuration;retain.replace(retain.find("private-test-token"),18,"");assert(home::setup_request("POST","/api/config",retain.c_str(),response,sizeof response,nullptr)==200);assert(std::string(home::config.token)=="private-test-token");
 assert(home::setup_request("POST","/api/discover",R"({"offset":0,"query":"desk","kind":"light","hide_unavailable":true})",response,sizeof response,nullptr)==200);step(1000);assert(url.find("/api/template")!=std::string::npos&&payload.find("desk")!=std::string::npos);reply(200,R"({"entities":[{"id":"light.desk","name":"Desk light","available":true}],"total":1,"offset":0})");assert(home::discovery_total==1);assert(home::setup_request("GET","/api/status","",response,sizeof response,nullptr)==200&&strstr(response,"light.desk"));ticks+=600001;native_app::setup::poll();step();assert(!native_app::setup::is_active());close_app();
 launch("home-assistant-preview",home::home_entry,home::tick,true);assert(native_app::setup::is_active());home::back(nullptr);assert(closed&&!native_app::setup::is_active());close_app();assert(cfg.load("home-assistant")&&cfg.get("configuration","")==configuration);
 // Repeated source lifecycle, both entry paths, no stale UI handles or server.
 for(int i=0;i<20;++i){launch("transit-preview",transit::transit_entry,transit::tick,i%2);close_app();launch("planes-preview",planes::planes_entry,planes::tick,i%2);close_app();launch("home-assistant-preview",home::home_entry,home::tick,i%2);close_app();assert(!native_app::setup::is_active());}
 // Real installed-app settings dispatch, Back target and uninstall cleanup.
 auto catalog=native_app::simulator::catalog();std::string error;
 for(const char*id:{"transit","planes","home-assistant"}){bool found=false;for(auto&item:catalog)if(item.id==id){assert(native_app::simulator::install(item,error));found=true;}assert(found);}
 settings_launch_with_route("settings_apps");lv_timer_handler();assert(settings_apps_active());
 for(int cycle=0;cycle<3;++cycle)for(const char*id:{"transit","planes","home-assistant"}){
  assert(native_app::launch_settings_id(id));native_app::poll();ticks+=50;lv_timer_handler();assert(!settings_apps_active());
  auto*l=find_label(lv_screen_active(),std::string(id)=="home-assistant"?"Done":LV_SYMBOL_LEFT);assert(l);lv_obj_send_event(lv_obj_get_parent(l),LV_EVENT_CLICKED,nullptr);ticks+=50;lv_timer_handler();native_app::poll();assert(settings_apps_active()&&!native_app::busy());
 }
 for(const char*id:{"transit","planes","home-assistant"}){assert(native_app::store::uninstall(id,error));assert(!std::ifstream(std::string(".pio/native-app-settings/")+id+"-preview.config.json"));}
 std::cout<<"PASS: phase 3 migration, app parsers, settings, HTTP fencing, aircraft fallback/routes, Home Assistant actions, paired setup, expiry and 60 cleanup cycles\n";
}
