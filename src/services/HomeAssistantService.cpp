#if !GADGET_OS_NATIVE_APP_LOADER && !GADGET_OS_APP_STORE_SIMULATOR
#include "services/HomeAssistantService.h"
#include "services/HomeAssistantHttp.h"
#include "services/HomeAssistantDiscovery.h"
#include "services/ConfigStoreService.h"
#include "services/TransitService.h"
#include "services/UpdateService.h"
#include "services/WifiService.h"
#include <ArduinoJson.h>
#include <algorithm>
#include <cstdlib>
#if defined(ARDUINO)
#include <Arduino.h>
#include <esp_system.h>
#else
#include <thread>
#include <random>
#endif
namespace {uint32_t now(){return TransitService::now_ms();}}
HomeAssistantService&HomeAssistantService::instance(){static HomeAssistantService s;return s;}
bool HomeAssistantService::begin(){auto&cfg=ConfigStoreService::instance();if(!cfg.begin()||!cfg.load("home-assistant"))return false;std::string error;auto raw=cfg.get("configuration","");if(!raw.empty()&&!ha::parse_config(raw,_config,error))_notice="Home configuration invalid - configure again";return true;}
void HomeAssistantService::resume(){_active=true;_next=0;}
void HomeAssistantService::pause(){_active=false;_actions.clear();close_setup();++_generation;}
bool HomeAssistantService::connected()const{return _online&&now()-_seen<30000;}
const ha::Entity*HomeAssistantService::entity(const std::string&id)const{for(auto&e:_entities)if(e.id==id)return &e;return nullptr;}
bool HomeAssistantService::configure(const std::string&raw,std::string&error){
 if(_running){error="Please wait for the current request";return false;}ha::Config c;if(!ha::parse_config(raw,c,error,&_config))return false;
 auto&cfg=ConfigStoreService::instance();if(!cfg.load("home-assistant")){error="Cannot open settings";return false;}cfg.set("configuration",ha::config_json(c,true));if(!cfg.save()){error="Cannot save settings";return false;}
 _actions.clear();_config=std::move(c);_entities.clear();_discovery.clear();_discovery_total=0;_online=false;_notice.clear();_next=0;++_generation;++_revision;return true;
}
bool HomeAssistantService::discover(size_t offset,const std::string&query,const std::string&kind,bool hide_unavailable){
 if(_running||offset>100000||query.size()>80||(!kind.empty()&&!ha::valid_entity(kind+".example")))return false;
 _discovery_error.clear();_job_offset=offset;_job_hide_unavailable=hide_unavailable;return start(1,query,kind);
}
bool HomeAssistantService::pending(const std::string&id)const{for(auto&a:_actions)if(a.id==id)return true;return false;}
const char*HomeAssistantService::expected_state(const std::string&id)const{for(auto&a:_actions)if(a.id==id&&!a.expected.empty())return a.expected.c_str();return nullptr;}
int HomeAssistantService::expected_value(const std::string&id,const std::string&action,int fallback)const{for(auto&a:_actions)if(a.id==id&&a.control==action)return a.value;return fallback;}
bool HomeAssistantService::act(const std::string&id,const std::string&action,int value){
 const auto*e=entity(id);std::string path,body;if(pending(id)||_actions.size()>=ha::MaxPages*ha::MaxTiles||!connected()||!e||!ha::command(_config,*e,action,value,path,body))return false;
 std::string expected;if(ha::domain(id)=="light"&&action=="toggle")expected=e->state=="on"?"off":"on";
 if(action=="brightness"||action=="warmth")expected=action=="brightness"&&value==0?"off":"on";
 _actions.push_back({id,path,body,expected,now()+30000,++_action_serial,false,false,action,value});++_revision;dispatch();return true;
}
void HomeAssistantService::dispatch(){
 if(_running)return;
 for(auto&a:_actions)if(!a.sent){if(start(2,a.path,a.body)){a.sent=true;_job_action=a.serial;}return;}
}
bool HomeAssistantService::start(int kind,const std::string&path,const std::string&body){
 if(_running||_config.token.empty())return false;
#if defined(ARDUINO)
 if(!WifiService::instance().is_connected()){_online=false;_notice="Wi-Fi disconnected";++_revision;return false;}
 auto state=UpdateService::instance().state();if(state==UpdateService::State::Checking||state==UpdateService::State::Downloading||state==UpdateService::State::ReadyToReboot)return false;
#endif
 _job=kind;_job_config=_config;_job_path=path;_job_body=body;_job_generation=_generation;_result.clear();_result_error.clear();_running=true;_finished.store(false,std::memory_order_relaxed);++_revision;
#if defined(ARDUINO)
 if(xTaskCreate(worker,"home-assistant",12288,this,1,nullptr)!=pdPASS){_result_error="Not enough memory for request";_finished.store(true,std::memory_order_release);}
#else
 std::thread(worker,this).detach();
#endif
 return true;
}
void HomeAssistantService::worker(void*ptr){
 auto&s=*static_cast<HomeAssistantService*>(ptr);
 { // Destroy response buffers before publishing completion and deleting the task.
 std::string raw;
 if(s._job==2){ha::request(s._job_config,s._job_path,s._job_body,raw,s._result_error);}
 else if(s._job==1){
  int status=0;
  if(ha::request(s._job_config,"/api/template",ha::discovery_request(s._job_offset,s._job_path,s._job_body,s._job_hide_unavailable),raw,s._result_error,nullptr,&status))
   ha::parse_discovery(raw,s._job_offset,s._result,s._result_total,s._result_error);
  else if(status==403)s._result_error="Entity search requires a Home Assistant administrator token. You can still add entities by ID.";
 }
 else {
  // Poll only configured entities, never the entire installation during normal use.
  for(auto&p:s._job_config.pages)for(auto&t:p.tiles){
   if(std::any_of(s._result.begin(),s._result.end(),[&](const ha::Entity&e){return e.id==t.id;}))continue;
   if(!ha::request(s._job_config,"/api/states/"+t.id,"",raw,s._result_error)){
    if(s._result_error=="Entity no longer exists"){ha::Entity missing;missing.id=t.id;missing.state="unavailable";s._result.push_back(missing);s._result_error.clear();continue;}goto done;
   }
   ha::Entity entity;if(!ha::parse_entity(raw,entity,s._result_error))goto done;
   if(entity.id!=t.id){s._result_error="Invalid Home Assistant entity";goto done;}s._result.push_back(std::move(entity));
  }
 }
 done:;
 }
 s._finished.store(true,std::memory_order_release);
#if defined(ARDUINO)
 vTaskDelete(nullptr);
#endif
}
void HomeAssistantService::loop(){
 if(_running&&_finished.load(std::memory_order_acquire)){
  _running=false;
  if(_job_generation==_generation){
   if(_job==1)_discovery_error=_result_error;
   if(!_result_error.empty()){_notice=_result_error;_notice_until=now()+5000;}else if(!_notice_until)_notice.clear();
   _online=_result_error.empty();
   if(_online){_seen=now();if(_job==0)_entities=std::move(_result);if(_job==1){_discovery=std::move(_result);_discovery_total=_result_total;_discovery_offset=_job_offset;_discovery_query=_job_path;_discovery_kind=_job_body;_discovery_hide_unavailable=_job_hide_unavailable;}}
   for(auto i=_actions.begin();i!=_actions.end();){
    bool remove=false;
    if(_job==2&&i->serial==_job_action){i->accepted=_online;remove=!_online||i->expected.empty();}
    if(_job==0&&i->accepted){
     auto*e=entity(i->id);bool confirmed=e&&e->state==i->expected;
     // Ignore stale polls and intermediate transition values. Allow device rounding.
     if(confirmed&&i->control=="brightness"&&i->value>0)confirmed=e->brightness>=0&&std::abs((e->brightness*100+127)/255-i->value)<=1;
     if(confirmed&&i->control=="warmth")confirmed=e->kelvin>0&&std::abs(e->kelvin-i->value)<=50;
     remove=!_online||confirmed;
    }
    if(remove)i=_actions.erase(i);else ++i;
   }
   _next=now()+(_job==2&&_online?250:_online?(_actions.empty()?5000:1000):15000);++_revision;
  }
  _job_config={};_job_body.clear();_result.clear();
 }
 if(_notice_until&&int32_t(now()-_notice_until)>=0){_notice_until=0;_notice.clear();++_revision;}
 for(auto i=_actions.begin();i!=_actions.end();)if(int32_t(now()-i->deadline)>=0){i=_actions.erase(i);_notice="Home action not confirmed";_notice_until=now()+5000;++_revision;}else ++i;
 dispatch();
 if(setup_open()){if(int32_t(now()-_setup_until)>=0){close_setup();++_revision;}else ha::server_loop();}
 if(_active&&!setup_open()&&!_running&&!_config.token.empty()&&!_config.pages.empty()&&int32_t(now()-_next)>=0){_next=now()+5000;start(0);}
}
bool HomeAssistantService::open_setup(){
 if(setup_open()){_setup_until=now()+600000;return true;}
 if(!ha::server_start()){_notice="Cannot start setup server";++_revision;return false;}
 _setup_key.clear();for(int i=0;i<4;++i){uint32_t r;
#if defined(ARDUINO)
 r=esp_random();
#else
 r=std::random_device{}();
#endif
 char b[9];snprintf(b,sizeof b,"%08x",unsigned(r));_setup_key+=b;}
 _setup_until=now()+600000;++_revision;return true;
}
void HomeAssistantService::close_setup(){ha::server_stop();_setup_key.clear();_next=0;}
bool HomeAssistantService::setup_open()const{return !_setup_key.empty();}
std::string HomeAssistantService::setup_url()const{return setup_open()?ha::server_address()+"/#"+_setup_key:"";}
std::string HomeAssistantService::setup_request(const std::string&method,const std::string&path,const std::string&key,const std::string&body,int&status){
 status=200;JsonDocument d;
 if(!setup_open()||key!=_setup_key){status=403;return "{\"error\":\"Open setup on the device and use its link\"}";}
 _setup_until=now()+600000;
 if(method=="GET"&&path=="/api/config")return ha::config_json(_config);
 if(method=="GET"&&path=="/api/status"){
  d["total"]=_discovery_total;d["offset"]=_discovery_offset;d["page_size"]=ha::DiscoveryPageSize;d["query"]=_discovery_query;d["kind"]=_discovery_kind;d["hide_unavailable"]=_discovery_hide_unavailable;d["busy"]=_running;d["error"]=_discovery_error;d["connected"]=connected();auto a=d["entities"].to<JsonArray>();for(auto&e:_discovery){auto o=a.add<JsonObject>();o["id"]=e.id;o["name"]=e.name;o["available"]=e.available();}
 }else if(method=="POST"&&path=="/api/config"){
  std::string error;if(!configure(body,error)){status=400;d["error"]=error;}else d["ok"]=true;
 }else if(method=="POST"&&path=="/api/discover"){
  JsonDocument args;auto invalid=deserializeJson(args,body);
  if(invalid||!args.is<JsonObject>()||(args["offset"]|0)<0){status=400;d["error"]="Invalid discovery request";}
  else if(!discover(args["offset"]|0,args["query"]|"",args["kind"]|"",args["hide_unavailable"]|true)){status=409;d["error"]="Cannot connect yet; check configuration or wait";}else d["ok"]=true;
 }else{status=404;d["error"]="Not found";}
 std::string out;serializeJson(d,out);return out;
}
SERVICE_REGISTER(HomeAssistantService);

#endif
