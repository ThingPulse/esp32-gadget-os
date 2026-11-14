/* Desktop executes the same source as each installable package. */
#ifndef ARDUINO
#include "experimental/NativeUi.h"
#include "experimental/NativeServices.h"
#include "experimental/NativeSettings.h"
#include "experimental/NativeSetup.h"
#include "ui/UiNav.h"
#include <atomic>
#include <thread>
#include <memory>
#include <fstream>
#include <string>
#include <cstdlib>
#include <cstdio>
#include <unistd.h>
#include <sys/wait.h>
#include <algorithm>
#include <cctype>
#define gadget_app_entry transit_entry
namespace transit_package {
#include "../../experiments/native-app/example/transit.c"
}
#undef gadget_app_entry
#define gadget_app_entry planes_entry
namespace planes_package {
#include "../../experiments/native-app/example/planes.c"
}
#undef gadget_app_entry
#define gadget_app_entry home_entry
namespace home_package {
#include "../../experiments/native-app/example/home_assistant.c"
}
#undef gadget_app_entry
namespace {
namespace services=native_app::services;
lv_obj_t*home=nullptr;lv_timer_t*timer=nullptr,*poller=nullptr;bool closing=true,settings_mode=false;
gadget_app_lifecycle lifecycle{};std::string app_id;uint32_t response_retry=0;
struct Request{std::atomic<bool>done{false};std::string body;int status=GADGET_HTTP_TRANSPORT;uint32_t retry=0;};
std::shared_ptr<Request>request;gadget_http_callback callback=nullptr;void*callback_user=nullptr;
void dispatch(gadget_callback cb,void*u){if(!closing&&cb)cb(u);}
void stop(){if(closing)return;closing=true;native_app::setup::stop();native_app::ui::suspend();callback=nullptr;callback_user=nullptr;lv_async_call([](void*){if(timer)lv_timer_delete(timer);if(poller)lv_timer_delete(poller);timer=poller=nullptr;if(lifecycle.shutdown)lifecycle.shutdown();native_app::ui::end();native_app::settings::end();services::end();ui_load_screen(home,true);},nullptr);}
gadget_handle timer_create(uint32_t ms,gadget_callback cb,void*u){struct Data{gadget_callback cb;void*u;};static Data data;data={cb,u};timer=lv_timer_create([](lv_timer_t*t){auto*d=(Data*)lv_timer_get_user_data(t);dispatch(d->cb,d->u);},ms,&data);return timer;}
std::string quote(const std::string&s){std::string q="\"";for(char c:s){if(c=='\\'||c=='\"')q+='\\';if(c=='\n')q+="\\n";else if(c=='\r')q+="\\r";else q+=c;}return q+'"';}
void transport(std::shared_ptr<Request>r,std::string url,std::string method,std::string body,std::string token,std::string ca,unsigned cap){
 // Credentials and request bodies go through stdin, never process arguments.
 char ca_path[]="/tmp/gadget-app-ca-XXXXXX";int ca_fd=-1;
 if(!ca.empty()){ca_fd=mkstemp(ca_path);if(ca_fd<0)return;bool ok=write(ca_fd,ca.data(),ca.size())==ssize_t(ca.size());close(ca_fd);if(!ok){unlink(ca_path);return;}}
 std::string config="silent\nmax-time = 25\nmax-filesize = "+std::to_string(cap)+"\nurl = "+quote(url)+"\nheader = \"Accept: application/json\"\nuser-agent = \"ThingPulse-GadgetOS/1.0\"\ninclude\nwrite-out = \"\\n%{http_code}\"\n";
 if(!token.empty())config+="header = "+quote("Authorization: Bearer "+token)+"\n";
 if(ca_fd>=0)config+="cacert = "+quote(ca_path)+"\n";
 if(method=="POST")config+="request = \"POST\"\nheader = \"Content-Type: application/json\"\ndata = "+quote(body)+"\n";
 char cfg_path[]="/tmp/gadget-app-request-XXXXXX";int fd=mkstemp(cfg_path);
 if(fd<0){if(ca_fd>=0)unlink(ca_path);return;}
 unlink(cfg_path);if(write(fd,config.data(),config.size())!=ssize_t(config.size())){close(fd);if(ca_fd>=0)unlink(ca_path);return;}
 lseek(fd,0,SEEK_SET);int pipes[2];if(pipe(pipes)){close(fd);if(ca_fd>=0)unlink(ca_path);return;}
 pid_t pid=fork();if(pid==0){dup2(fd,STDIN_FILENO);dup2(pipes[1],STDOUT_FILENO);close(fd);close(pipes[0]);close(pipes[1]);execlp("curl","curl","-q","--config","-",static_cast<char*>(nullptr));_exit(127);}
 close(fd);close(pipes[1]);char buf[2048];bool large=false;ssize_t n;
 if(pid>0)while((n=read(pipes[0],buf,sizeof buf))>0){if(r->body.size()+n<=cap+8192)r->body.append(buf,n);else large=true;}
 close(pipes[0]);int status=0;if(pid>0)waitpid(pid,&status,0);if(ca_fd>=0)unlink(ca_path);
 auto end=r->body.rfind('\n');if(pid>0&&WIFEXITED(status)&&WEXITSTATUS(status)==0&&!large&&end!=std::string::npos){r->status=atoi(r->body.c_str()+end+1);r->body.resize(end);size_t header_bytes=0;while(r->body.compare(0,5,"HTTP/")==0){auto split=r->body.find("\r\n\r\n");if(split==std::string::npos||header_bytes+split+4>8192){r->status=GADGET_HTTP_TRANSPORT;r->body.clear();break;}auto headers=r->body.substr(0,split);std::transform(headers.begin(),headers.end(),headers.begin(),[](unsigned char c){return std::tolower(c);});auto retry=headers.find("\r\nretry-after:");if(retry!=std::string::npos)r->retry=std::min(21600UL,strtoul(headers.c_str()+retry+14,nullptr,10));header_bytes+=split+4;r->body.erase(0,split+4);}}
 if(large||r->body.size()>cap){r->status=GADGET_HTTP_TOO_LARGE;r->body.clear();}
}
int32_t request_http(const gadget_http_request*args,gadget_http_callback cb,void*u){
 if(request||closing)return GADGET_HTTP_BUSY;
 if(!args||args->struct_size<sizeof(*args)||!cb||!args->url||!args->method||!args->body||!args->bearer||!args->ca||!args->response_limit||args->response_limit>196608)return GADGET_HTTP_INVALID;
 std::string url=args->url,method=args->method,body=args->body,token=args->bearer,ca=args->ca;
 size_t start=url.compare(0,8,"https://")==0?8:url.compare(0,7,"http://")==0?7:0;
 if(!start||url.size()<=start||url.size()>2047||url.find_first_of("@#")!=std::string::npos||(method!="GET"&&method!="POST")||body.size()>24576||token.size()>4096||ca.size()>8192)return GADGET_HTTP_INVALID;
 for(unsigned char c:url)if(c<=32||c>=127)return GADGET_HTTP_INVALID;
 auto authority=url.substr(start,url.find('/',start)-start);if(authority.empty()||authority.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789.-:[]")!=std::string::npos||token.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789._-=")!=std::string::npos)return GADGET_HTTP_INVALID;
 const char*env=app_id=="transit"?"GADGET_OS_TRANSIT_FIXTURE_DIR":app_id=="planes"?"GADGET_OS_PLANES_FIXTURE_DIR":"GADGET_OS_HOME_FIXTURE_DIR";
 std::string fixture=getenv(env)?getenv(env):"";unsigned cap=args->response_limit;
 auto r=std::make_shared<Request>();request=r;callback=cb;callback_user=u;
 std::thread([r,url,method,body,token,ca,fixture,cap]{if(fixture.empty())transport(r,url,method,body,token,ca,cap);else{std::string name=url.find("geocode")!=std::string::npos?"search":url.find("stoptimes")!=std::string::npos?"board":url.find("v6/trip")!=std::string::npos?"trip":url.find("callsign/")!=std::string::npos?"route":url.find("/api/template")!=std::string::npos?"discovery":url.find("/api/services/")!=std::string::npos?"action":url.find("/api/states/")!=std::string::npos?url.substr(url.rfind('/')+1):"planes";std::ifstream file(fixture+"/"+name+".json");if(file){char c;while(file.get(c)&&r->body.size()<=cap)r->body+=c;r->status=r->body.size()>cap?GADGET_HTTP_TOO_LARGE:200;}else r->status=404;}r->done.store(true,std::memory_order_release);}).detach();return 0;
}
int32_t get(const char*url,uint32_t cap,gadget_http_callback cb,void*u){if(!url||strncmp(url,"https://",8))return GADGET_HTTP_INVALID;gadget_http_request r{sizeof r,url,"GET","","","",cap};return request_http(&r,cb,u);}
void launch(const char*id,bool settings,int(*entry)(int,char**)){
 app_id=id;home=lv_screen_active();auto*screen=lv_obj_create(nullptr);closing=false;settings_mode=settings;
 native_app::ui::begin(screen,dispatch);native_app::settings::begin((app_id+"-preview").c_str());services::begin((app_id+"-preview").c_str());
 static const gadget_services_api s={sizeof(s),services::environment,services::format_time,services::parse_time,services::json_open,services::json_get,services::json_count,services::json_close,services::cache_read,services::cache_write,nullptr,get,[]()->uint32_t{return response_retry;},services::json_number,services::json_real,services::format_number,services::monotonic_ms,&services::math_api,native_app::settings::config_read,native_app::settings::config_write,request_http,[](const void*p,uint32_t n,gadget_setup_callback cb,void*u,char*out,uint32_t cap)->int32_t{return closing?-1:native_app::setup::start(p,n,cb,u,out,cap);},native_app::setup::stop,native_app::setup::is_active};
 static gadget_host_api api{};api.struct_size=sizeof api;api.major=1;api.minor=10;api.ui=&native_app::ui::api;api.services=&s;api.timer_create=timer_create;api.request_close=stop;api.setting_get=native_app::settings::get;api.setting_set=native_app::settings::set;api.launch_mode=[]()->uint32_t{return settings_mode?GADGET_LAUNCH_SETTINGS:GADGET_LAUNCH_NORMAL;};
 lifecycle={sizeof lifecycle};char*args[]={reinterpret_cast<char*>(&api),reinterpret_cast<char*>(&lifecycle)};ui_load_screen(screen,false);if(entry(2,args)){stop();return;}
 poller=lv_timer_create([](lv_timer_t*){if(closing)return;native_app::setup::poll();if(request&&request->done.load(std::memory_order_acquire)){auto r=request;request.reset();response_retry=r->retry;auto cb=callback;auto*u=callback_user;callback=nullptr;callback_user=nullptr;if(cb)cb(r->status,r->body.data(),r->body.size(),u);}},25,nullptr);
}
}
void transit_simulator_launch(){launch("transit",false,transit_package::transit_entry);}
void transit_settings_simulator_launch(){launch("transit",true,transit_package::transit_entry);}
void planes_simulator_launch(){launch("planes",false,planes_package::planes_entry);}
void planes_settings_simulator_launch(){launch("planes",true,planes_package::planes_entry);}
void home_assistant_simulator_launch(){launch("home-assistant",false,home_package::home_entry);}
void home_assistant_settings_simulator_launch(){launch("home-assistant",true,home_package::home_entry);}
#endif
