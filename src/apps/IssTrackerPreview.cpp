/* SDL adapter executes exactly the installable C source, using shared SDK hosts. */
#ifndef ARDUINO
#include "AppRegistry.h"
#include "apps/SettingsApp.h"
#include "experimental/NativeUi.h"
#include "experimental/NativeServices.h"
#include "ui/UiNav.h"
#include <atomic>
#include <thread>
#include <memory>
#include <fstream>
#include <iterator>
#include <string>
#include <cstdio>
#include <cstdlib>
#define gadget_app_entry iss_tracker_preview_entry
namespace iss_tracker_source {
#include "../../experiments/native-app/example/iss_tracker.c"
}
#undef gadget_app_entry
namespace {
namespace services=native_app::services;
void launch();
lv_obj_t*home;lv_timer_t*timer,*poller;bool closing=false,location_requested=false;gadget_app_lifecycle lifecycle;
struct Request {std::atomic<bool> done{false};std::string body;int status=-5;};
std::shared_ptr<Request> request;gadget_http_callback callback;void*callback_user;
void dispatch(gadget_callback cb,void*u){if(!closing&&cb)cb(u);}
void stop(){if(closing)return;closing=true;native_app::ui::suspend();callback=nullptr;request.reset();lv_async_call([](void*){if(timer)lv_timer_delete(timer);if(poller)lv_timer_delete(poller);timer=poller=nullptr;if(lifecycle.shutdown)lifecycle.shutdown();native_app::ui::end();services::end();ui_load_screen(home,true);if(location_requested){location_requested=false;settings_launch_with_route("settings_weather");lv_timer_create([](lv_timer_t*t){if(lv_screen_active()==home){lv_timer_delete(t);launch();}},100,nullptr);}},nullptr);}
gadget_handle timer_create(uint32_t ms,gadget_callback cb,void*u){struct Timer{gadget_callback cb;void*u;};static Timer data;data={cb,u};timer=lv_timer_create([](lv_timer_t*t){auto*d=(Timer*)lv_timer_get_user_data(t);dispatch(d->cb,d->u);},ms,&data);return timer;}
int32_t get(const char*url,uint32_t cap,gadget_http_callback cb,void*u){if(request||closing)return GADGET_HTTP_BUSY;std::string address=url?url:"";if(cap>49152||address.find("https://iss-api.polluxlabs.io/iss-pass?")!=0||address.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789:/?=._-&")!=std::string::npos)return GADGET_HTTP_INVALID;
 auto r=std::make_shared<Request>();request=r;callback=cb;callback_user=u;std::string fixtures=getenv("GADGET_OS_ISS_FIXTURE_DIR")?getenv("GADGET_OS_ISS_FIXTURE_DIR"):"";
 std::thread([r,address,fixtures,cap]{if(!fixtures.empty()){std::ifstream f(fixtures+(address.find("visible_only=true")!=std::string::npos?"/visible.json":"/all.json"));r->body.assign(std::istreambuf_iterator<char>(f),{});r->status=200;}else{std::string command="curl -sS --max-time 25 --max-filesize 49152 -w '\\n%{http_code}' '"+address+"'";auto*p=popen(command.c_str(),"r");if(p){char b[2048];while(size_t n=fread(b,1,sizeof(b),p)){if(r->body.size()+n<=cap+8)r->body.append(b,n);else r->status=GADGET_HTTP_TOO_LARGE;}int exit=pclose(p);auto pos=r->body.rfind('\n');if(exit==0&&pos!=std::string::npos&&r->status!=GADGET_HTTP_TOO_LARGE){r->status=atoi(r->body.c_str()+pos+1);r->body.resize(pos);}}}if(r->body.size()>cap){r->body.clear();r->status=GADGET_HTTP_TOO_LARGE;}r->done=true;}).detach();return 0;}
void launch(){home=lv_screen_active();auto*screen=lv_obj_create(nullptr);closing=false;timer=poller=nullptr;native_app::ui::begin(screen,dispatch);services::begin("iss-tracker-preview");
 static const gadget_services_api s={sizeof(s),services::environment,services::format_time,services::parse_time,services::json_open,services::json_get,services::json_count,services::json_close,services::cache_read,services::cache_write,[](){location_requested=true;stop();},get,[]()->uint32_t{return 0;},services::json_number};
 static gadget_host_api api{};api.struct_size=sizeof(api);api.major=1;api.minor=5;api.ui=&native_app::ui::api;api.services=&s;api.timer_create=timer_create;api.request_close=stop;lifecycle={sizeof(lifecycle)};char*args[]={(char*)&api,(char*)&lifecycle};ui_load_screen(screen,false);iss_tracker_source::iss_tracker_preview_entry(2,args);
 poller=lv_timer_create([](lv_timer_t*){if(request&&request->done.load()&&!closing){auto r=request;request.reset();auto cb=callback;auto*u=callback_user;callback=nullptr;if(cb)cb(r->status,r->body.data(),r->body.size(),u);}},50,nullptr);
}
#if !GADGET_OS_APP_STORE_SIMULATOR
APP_REGISTER_COLOR_RANK("ISS\nPreview",LV_SYMBOL_GPS,launch,0x237e77,92);
#endif
}
void iss_tracker_simulator_launch(){launch();}
#endif
