#include "experimental/NativeUi.h"
#include "experimental/NativeServices.h"
#include "services/IssService.h"
#include "ui/I18n.h"
#include <cassert>
#include <cstring>
#include <ctime>
#include <fstream>
#include <iostream>
#include <string>
#define gadget_app_entry tracker_entry
namespace app {
#include "../../experiments/native-app/example/iss_tracker.c"
}
#undef gadget_app_entry
namespace ui=native_app::ui;namespace svc=native_app::services;
static gadget_environment environment;
static gadget_http_callback response_cb;static void*response_user;
static std::string requested;static unsigned requests=0,retry=0;static bool closed=false,location_open=false;
static gadget_callback ticker;static void*ticker_user;
static uint8_t frame[320*480*3],buffer[320*48*4];
static int32_t environment_read(gadget_environment*out){*out=environment;return 0;}
static int32_t http_get(const char*url,uint32_t cap,gadget_http_callback cb,void*u){assert(cap==49152);assert(!response_cb);response_cb=cb;response_user=u;requested=url;++requests;return 0;}
static void deliver(int status,const std::string&body){auto cb=response_cb;auto u=response_user;response_cb=nullptr;assert(cb);cb(status,body.c_str(),body.size(),u);}
static void dispatch(gadget_callback cb,void*u){if(!closed)cb(u);}
static lv_obj_t*find(lv_obj_t*o,const char*s){if(lv_obj_check_type(o,&lv_label_class)&&!strcmp(lv_label_get_text(o),s))return o;for(unsigned i=0;i<lv_obj_get_child_count(o);++i)if(auto*r=find(lv_obj_get_child(o,i),s))return r;return nullptr;}
static void click(const char*s){auto*l=find(lv_screen_active(),s);assert(l);lv_obj_send_event(lv_obj_get_parent(l),LV_EVENT_CLICKED,nullptr);}
static std::string iso(uint32_t stamp){time_t t=stamp;tm value{};gmtime_r(&t,&value);char b[32];strftime(b,sizeof(b),"%Y-%m-%dT%H:%M:%SZ",&value);return b;}
static std::string payload(uint32_t rise,bool visible=true){return "{\"tle_stale\":false,\"passes\":[{\"rise\":{\"time\":\""+iso(rise)+"\",\"azimuth_deg\":247.5},\"culmination\":{\"time\":\""+iso(rise+180)+"\",\"elevation_deg\":62},\"set\":{\"time\":\""+iso(rise+420)+"\",\"azimuth_deg\":45},\"visible\":"+(visible?"true":"false")+",\"visible_start\":\""+iso(rise+30)+"\",\"visible_end\":\""+iso(rise+330)+"\"}]}";}
static void check_text(lv_obj_t*o){if(lv_obj_check_type(o,&lv_label_class)&&lv_label_get_long_mode(o)!=LV_LABEL_LONG_DOT){lv_point_t size;lv_text_get_size(&size,lv_label_get_text(o),lv_obj_get_style_text_font(o,LV_PART_MAIN),0,0,lv_obj_get_content_width(o),LV_TEXT_FLAG_NONE);if(size.y>lv_obj_get_content_height(o)){std::cerr<<"Clipped: "<<lv_label_get_text(o)<<" height="<<size.y<<" available="<<lv_obj_get_content_height(o)<<"\n";assert(false);}}for(unsigned i=0;i<lv_obj_get_child_count(o);++i)check_text(lv_obj_get_child(o,i));}
static void capture(const char*dir,const char*name){lv_obj_update_layout(lv_screen_active());check_text(lv_screen_active());lv_refr_now(nullptr);std::ofstream f(std::string(dir)+"/iss-installable-"+name+".ppm",std::ios::binary);f<<"P6\n320 480\n255\n";f.write((char*)frame,sizeof(frame));}
static void tick(unsigned seconds=1){environment.epoch+=seconds;ticker(ticker_user);}
static const gadget_services_api services={sizeof(services),environment_read,svc::format_time,svc::parse_time,svc::json_open,svc::json_get,svc::json_count,svc::json_close,svc::cache_read,svc::cache_write,[](){location_open=true;closed=true;},http_get,[]()->uint32_t{return retry;},svc::json_number};
static gadget_host_api api;static gadget_app_lifecycle lifecycle;
static void open(){closed=false;response_cb=nullptr;ui::begin(lv_screen_active(),dispatch);svc::begin("iss-test");lifecycle={sizeof(lifecycle)};char*args[]={(char*)&api,(char*)&lifecycle};assert(app::tracker_entry(2,args)==0);}
static void close(){response_cb=nullptr;ui::suspend();lifecycle.shutdown();ui::end();svc::end();lv_obj_clean(lv_screen_active());}
int main(int argc,char**argv){assert(argc==2);lv_init();lv_tick_set_cb([](){return 1U;});auto*d=lv_display_create(320,480);lv_display_set_color_format(d,LV_COLOR_FORMAT_XRGB8888);lv_display_set_buffers(d,buffer,nullptr,sizeof(buffer),LV_DISPLAY_RENDER_MODE_PARTIAL);lv_display_set_flush_cb(d,[](lv_display_t*d,const lv_area_t*a,uint8_t*p){int w=a->x2-a->x1+1;for(int y=a->y1;y<=a->y2;++y)for(int x=a->x1;x<=a->x2;++x){int dst=(y*320+x)*3,src=((y-a->y1)*w+x-a->x1)*4;frame[dst]=p[src+2];frame[dst+1]=p[src+1];frame[dst+2]=p[src];}lv_display_flush_ready(d);});lv_sysmon_hide_performance(d);
 api={};api.struct_size=sizeof(api);api.major=1;api.minor=5;api.ui=&ui::api;api.services=&services;api.timer_create=[](uint32_t ms,gadget_callback cb,void*u)->gadget_handle{assert(ms==1000);ticker=cb;ticker_user=u;return (void*)1;};api.request_close=[](){closed=true;};
 environment={sizeof(environment),(uint32_t)time(nullptr),1,1,0,1,1,47376900,8541700,"Zurich"};uint32_t base=environment.epoch;
 open();assert(requested.find("lat=47.376900&lon=8.541700")!=std::string::npos);assert(find(lv_screen_active(),"Loading predictions..."));assert(find(lv_screen_active(),"Updating predictions..."));capture(argv[1],"loading");auto json=payload(base+600);auto reference=iss::parse(json);assert(reference.error.empty());deliver(200,json);assert(!response_cb);tick(5);assert(requested.find("visible_only=true")!=std::string::npos);deliver(200,json);assert(app::forecast.count==1);assert(app::forecast.passes[0].rise==reference.passes[0].rise&&app::forecast.passes[0].rise_az==2475);capture(argv[1],"next");assert(find(lv_screen_active(),"Next visible pass"));
 click("Passes");capture(argv[1],"passes");auto*list=lv_obj_get_child(lv_obj_get_child(lv_screen_active(),2),3); // Stable list across timer ticks.
 (void)list;auto*label=find(lv_screen_active(),"Upcoming passes");tick();assert(find(lv_screen_active(),"Upcoming passes")==label);click("All");assert(!app::visible_only);click("Next pass");
 environment.epoch=base+610;tick();assert(find(lv_screen_active(),"Pass in progress"));assert(find(lv_screen_active(),"Visible window starts in"));capture(argv[1],"active-delayed");tick(40);assert(find(lv_screen_active(),"Visible window ends in"));capture(argv[1],"active");tick(300);assert(find(lv_screen_active(),"Pass ends in"));capture(argv[1],"visibility-ended");
 // Manual requests obey the cooldown, and failures retain the previous forecast.
 app::allowed=app::ticks+60;unsigned before=requests;click(LV_SYMBOL_REFRESH);tick();assert(requests==before&&app::pending);for(int i=0;i<60;++i)tick();assert(response_cb);retry=120;deliver(429,"");assert(app::forecast.count==1&&app::allowed>=app::ticks+120);capture(argv[1],"rate-limit");
 // Cache survives close/reopen and is private to the app/location.
 close();environment.epoch=base+5;before=requests;open();assert(app::forecast.count==1&&requests==before);environment.online=0;tick();capture(argv[1],"offline");assert(find(lv_screen_active(),"Offline - cached predictions"));environment.online=1;
 app::allowed=0;app::pending=1;tick();assert(response_cb);auto late=response_cb;auto late_user=response_user;response_cb=nullptr;environment.latitude_e6+=100;tick();assert(app::forecast.count==0);late(200,json.c_str(),json.size(),late_user);assert(app::forecast.count==0);response_cb=nullptr;close();
 environment.has_location=0;open();capture(argv[1],"no-location");assert(find(lv_screen_active(),"Set location"));click("Set location");assert(location_open);close();environment.has_location=1;environment.latitude_e6=47376900;environment.clock_ready=0;open();capture(argv[1],"no-clock");close();environment.clock_ready=1;
 // Forty rows, scrollability, German layout, expiry and invisible selections.
 open();app::forecast.count=0;for(unsigned i=0;i<40;++i){app::Pass p={base+600+i*900,base+780+i*900,base+1020+i*900,base+630+i*900,base+930+i*900,2475,450,620,i%2};app::insert(&app::forecast,&p);}++app::revision;click("Passes");click("All");capture(argv[1],"forty-passes");assert(!closed);lv_obj_update_layout(lv_screen_active());auto*rows=lv_obj_get_child(lv_obj_get_child(lv_screen_active(),2),3);assert(lv_obj_get_child_count(rows)==40);lv_obj_scroll_to_y(rows,4000,LV_ANIM_OFF);lv_obj_update_layout(rows);assert(lv_obj_get_scroll_y(rows)>2000);lv_obj_send_event(lv_obj_get_child(rows,39),LV_EVENT_CLICKED,nullptr);assert(app::selected==base+600+39*900);app::choose((void*)1);assert(app::selected==base+1500);capture(argv[1],"selected-pass");
 environment.german=1;setenv("GADGET_OS_LANGUAGE","de",1);i18n::init();tick();capture(argv[1],"german-next");app::passes_page=1;app::shown_revision=~0U;app::render();capture(argv[1],"german-passes");close();open();capture(argv[1],"german-reopened");assert(find(lv_screen_active(),"Wo ist die ISS?"));assert(find(lv_screen_active(),"Überflüge"));close();
 // Empty/malformed responses and JSON/cache quotas.
 app::svc=&services;app::working={};assert(app::parse("{\"passes\":[]}",13));assert(!app::parse("{}",2));assert(!app::parse("invalid",7));assert(svc::json_open("{}",49153)==-1);assert(svc::json_count("passes")==-1);assert(svc::cache_write("x",16385)==-1);
 svc::begin("../escape");char private_data[16];assert(svc::cache_read(private_data,sizeof(private_data))==-1);svc::begin("other-app");assert(svc::cache_read(private_data,sizeof(private_data))==-1);svc::end();svc::begin("erase-test");assert(svc::cache_write("cached",6)==0);assert(svc::cache_read(private_data,sizeof(private_data))==6);svc::erase_cache("erase-test");assert(svc::cache_read(private_data,sizeof(private_data))==-1);svc::end();
 // New SDK bounds and stale handles after subtree replacement.
 ui::begin(lv_screen_active(),dispatch);auto root=ui::api.create(nullptr,GADGET_UI_ROOT,nullptr,nullptr,nullptr);auto old=ui::api.create(root,GADGET_UI_LABEL,"old",nullptr,nullptr);assert(ui::api.clear(root)==0);auto fresh=ui::api.create(root,GADGET_UI_LABEL,"fresh",nullptr,nullptr);assert(fresh!=old&&ui::api.text(old,"bad")==-1);assert(ui::api.reserve(257)==-1);ui::end();
 for(int i=0;i<100;++i){open();close();}
 std::cout<<"PASS: independent ISS parsing/parity, forecast merge, cache, rate-limit/cooldown, location/late-response, 40 rows, states, 100 lifecycles and SDK handle bounds\n";
}
