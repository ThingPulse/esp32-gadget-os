/* Native executes the exact package source with host-owned asynchronous I/O. */
#ifndef ARDUINO
#include "experimental/NativeUi.h"
#include "experimental/NativeServices.h"
#include "experimental/NativeSettings.h"
#include "ui/UiNav.h"
#include <atomic>
#include <algorithm>
#include <cctype>
#include <thread>
#include <memory>
#include <fstream>
#include <iterator>
#include <cstdio>
#include <cstdlib>
#include <string>
#define gadget_app_entry stocks_preview_entry
namespace stocks_source {
#include "../../experiments/native-app/example/stocks.c"
}
#undef gadget_app_entry
namespace {
namespace services=native_app::services;
lv_obj_t *home=nullptr;lv_timer_t *timer=nullptr,*poller=nullptr;
bool closing=true,settings_mode=false;gadget_app_lifecycle lifecycle{};
struct Request {std::atomic<bool> done{false};std::string body;int status=GADGET_HTTP_TRANSPORT;uint32_t retry=0;};
uint32_t response_retry=0;
std::shared_ptr<Request> request;gadget_http_callback callback=nullptr;void *callback_user=nullptr;
void dispatch(gadget_callback cb,void*u){if(!closing&&cb)cb(u);}
void stop(){if(closing)return;closing=true;native_app::ui::suspend();callback=nullptr;lv_async_call([](void*){
 if(timer)lv_timer_delete(timer);if(poller)lv_timer_delete(poller);timer=poller=nullptr;
 if(lifecycle.shutdown)lifecycle.shutdown();native_app::ui::end();native_app::settings::end();services::end();ui_load_screen(home,true);
},nullptr);}
gadget_handle timer_create(uint32_t ms,gadget_callback cb,void*u){struct Timer{gadget_callback cb;void*u;};static Timer data;data={cb,u};timer=lv_timer_create([](lv_timer_t*t){auto*d=static_cast<Timer*>(lv_timer_get_user_data(t));dispatch(d->cb,d->u);},ms,&data);return timer;}
int32_t get(const char*url,uint32_t cap,gadget_http_callback cb,void*u){
 if(request||closing)return GADGET_HTTP_BUSY;
 const std::string address=url?url:"",prefix="https://query1.finance.yahoo.com/v8/finance/chart/";
 if(!cb||!cap||cap>65536||address.compare(0,prefix.size(),prefix)||address.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789:/?=._-&")!=std::string::npos)return GADGET_HTTP_INVALID;
 const auto query=address.find('?',prefix.size());if(query==std::string::npos)return GADGET_HTTP_INVALID;
 const std::string symbol=address.substr(prefix.size(),query-prefix.size());
 for(char c:symbol)if(!((c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='.'||c=='-'))return GADGET_HTTP_INVALID;
 if(symbol.empty()||symbol.size()>24||symbol.front()=='.'||symbol.front()=='-')return GADGET_HTTP_INVALID;
 const std::string fixtures=getenv("GADGET_OS_STOCK_FIXTURE_DIR")?getenv("GADGET_OS_STOCK_FIXTURE_DIR"):"";
 auto r=std::make_shared<Request>();request=r;callback=cb;callback_user=u;
 std::thread([r,address,symbol,fixtures,cap]{
  if(!fixtures.empty()){
   std::ifstream f(fixtures+"/"+symbol+".json");if(f){r->status=200;char c;while(r->body.size()<=cap&&f.get(c))r->body+=c;}else r->status=404;
  }else{
   // URL alphabet and symbol were validated above; no shell syntax is accepted.
   const std::string command="curl -sS -D - --max-time 25 --max-filesize 65536 -A 'ThingPulse-GadgetOS/1.0' -w '\\n%{http_code}' '"+address+"'";
   if(auto*p=popen(command.c_str(),"r")){char buffer[2048];bool oversized=false;while(size_t n=fread(buffer,1,sizeof(buffer),p)){if(r->body.size()+n<=cap+8192)r->body.append(buffer,n);else oversized=true;}
    int exit=pclose(p);auto end=r->body.rfind('\n');if(!exit&&!oversized&&end!=std::string::npos){r->status=atoi(r->body.c_str()+end+1);r->body.resize(end);
     size_t header_bytes=0;
     while(r->body.compare(0,5,"HTTP/")==0){auto split=r->body.find("\r\n\r\n");if(split==std::string::npos||header_bytes+split+4>8192){r->status=GADGET_HTTP_TRANSPORT;r->body.clear();break;}
      std::string headers=r->body.substr(0,split);std::transform(headers.begin(),headers.end(),headers.begin(),[](unsigned char c){return std::tolower(c);});
      auto retry=headers.find("\r\nretry-after:");if(retry!=std::string::npos)r->retry=std::min(21600UL,std::strtoul(headers.c_str()+retry+14,nullptr,10));
      header_bytes+=split+4;r->body.erase(0,split+4);
     }
    }else if(oversized)r->status=GADGET_HTTP_TOO_LARGE;
   }
  }
  if(r->body.size()>cap){r->body.clear();r->status=GADGET_HTTP_TOO_LARGE;}r->done=true;
 }).detach();return 0;
}
void launch(bool settings){
 home=lv_screen_active();auto *screen=lv_obj_create(nullptr);closing=false;settings_mode=settings;
 native_app::ui::begin(screen,dispatch);native_app::settings::begin("stocks-preview");services::begin("stocks-preview");
 static const gadget_services_api s={sizeof(s),services::environment,services::format_time,services::parse_time,services::json_open,services::json_get,services::json_count,services::json_close,services::cache_read,services::cache_write,nullptr,get,[]()->uint32_t{return response_retry;},services::json_number,services::json_real,services::format_number,services::monotonic_ms,&services::math_api};
 static gadget_host_api api{};api.struct_size=sizeof(api);api.major=1;api.minor=11;api.ui=&native_app::ui::api;api.services=&s;api.timer_create=timer_create;api.request_close=stop;api.setting_get=native_app::settings::get;api.setting_set=native_app::settings::set;api.launch_mode=[]()->uint32_t{return settings_mode?GADGET_LAUNCH_SETTINGS:GADGET_LAUNCH_NORMAL;};
 lifecycle={sizeof(lifecycle)};char*args[]={reinterpret_cast<char*>(&api),reinterpret_cast<char*>(&lifecycle)};ui_load_screen(screen,false);
 if(stocks_source::stocks_preview_entry(2,args)){stop();return;}
 poller=lv_timer_create([](lv_timer_t*){if(request&&request->done.load()&&!closing){auto r=request;request.reset();response_retry=r->retry;auto cb=callback;auto *u=callback_user;callback=nullptr;if(cb)cb(r->status,r->body.data(),r->body.size(),u);}},50,nullptr);
}
}
void stocks_simulator_launch(){launch(false);}
void stocks_settings_simulator_launch(){launch(true);}
#endif
