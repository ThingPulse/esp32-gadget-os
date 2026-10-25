#include "ui/I18n.h"
#include "services/TransitService.h"
#include "services/ConfigStoreService.h"
#include "services/WifiService.h"
#include "services/TimeService.h"
#include "services/UpdateService.h"
#include <ArduinoJson.h>
#include <algorithm>
#if defined(ARDUINO)
#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#else
#include <chrono>
#include <thread>
#include <cstdlib>
#endif
TransitService &TransitService::instance(){static TransitService s;return s;}
uint32_t TransitService::now_ms(){
#if defined(ARDUINO)
 return millis();
#else
 return uint32_t(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
#endif
}
bool TransitService::online() const {
#if defined(ARDUINO)
 return WifiService::instance().is_connected()&&TimeService::instance().is_synchronized();
#else
 return true;
#endif
}
bool TransitService::begin(){
 auto &cfg=ConfigStoreService::instance();if(!cfg.begin()||!cfg.load("transit"))return false;
 JsonDocument doc;auto data=cfg.get("favourites","[]");
 if(cfg.get("provider","transitous")!="transitous")return true;
 if(data.size()>8192 || deserializeJson(doc,data) || !doc.is<JsonArray>())return true;
 for(JsonObjectConst o:doc.as<JsonArrayConst>()) {if(_favourites.size()==transit::MaxStops)break;
  transit::Stop s{o["id"]|"",o["name"]|"",o["area"]|""};if(s.id.empty()||s.id.size()>512||s.name.empty()||s.name.size()>96||s.area.size()>16)continue;
  if(std::none_of(_favourites.begin(),_favourites.end(),[&](const transit::Stop &v){return v.id==s.id;}))_favourites.push_back(s);
 }return true;
}
bool TransitService::save(const std::vector<transit::Stop>&items){
 JsonDocument doc;auto arr=doc.to<JsonArray>();for(const auto&s:items){auto o=arr.add<JsonObject>();o["id"]=s.id;o["name"]=s.name;o["area"]=s.area;}std::string data;serializeJson(doc,data);
 auto &cfg=ConfigStoreService::instance();if(!cfg.load("transit")){_notice=i18n::tr("Cannot open settings");return false;}cfg.set("provider",std::string("transitous"));cfg.set("favourites",data);
 if(!cfg.save()){_notice=i18n::tr("Cannot save favourites");return false;}_favourites=items;_notice.clear();++_revision;return true;
}
bool TransitService::add(const transit::Stop&s){
 if(s.id.empty()||s.id.size()>512||s.name.empty()||s.name.size()>96||s.area.size()>16){_notice=i18n::tr("Invalid stop");return false;}
 for(size_t i=0;i<_favourites.size();++i)if(_favourites[i].id==s.id){select(i);return true;}
 if(_favourites.size()==transit::MaxStops){_notice=i18n::tr("Maximum 8 favourite stops");return false;}auto next=_favourites;next.push_back(s);if(!save(next))return false;select(next.size()-1);return true;
}
bool TransitService::remove(size_t i){if(i>=_favourites.size())return false;auto next=_favourites;next.erase(next.begin()+i);if(!save(next))return false;_board={};_fetched=0;_auto=false;_pending=false;++_generation;_selected=0;if(!next.empty())select(0);return true;}
void TransitService::queue(transit::Request kind,const std::string&arg){++_generation;_kind=kind;_argument=arg;_pending=true;_notice.clear();++_revision;}
void TransitService::resume(){_active=true;_auto=true;if(!_favourites.empty()&&!_fetched)queue(transit::Request::Board,_favourites[_selected].id);}
void TransitService::select(size_t i){if(i>=_favourites.size())return;_active=true;_selected=i;_board={};_fetched=0;_auto=true;queue(transit::Request::Board,_favourites[i].id);}
void TransitService::search(const std::string&q){hold();_search={};if(q.size()<2||q.size()>80){_search.error=i18n::tr("Enter at least 2 characters");++_revision;return;}queue(transit::Request::Search,q);}
void TransitService::trip(const std::string&id){hold();_trip={};if(id.empty()){_trip.error=i18n::tr("No route details available");++_revision;return;}queue(transit::Request::Trip,id);}
void TransitService::refresh(){if(_favourites.empty())return;
 if(_fetched && now_ms()-_fetched<30000){_notice=i18n::tr("Up to date (30s cooldown)");++_revision;return;}_auto=true;queue(transit::Request::Board,_favourites[_selected].id);
}
void TransitService::worker(void *p){auto*s=static_cast<TransitService*>(p);s->_result=s->_provider.fetch(s->_job_kind,s->_job_argument);s->_finished.store(true,std::memory_order_release);
#if defined(ARDUINO)
 vTaskDelete(nullptr);
#endif
}
void TransitService::loop(){
 uint32_t now=now_ms();
 if(_running&&_finished.load(std::memory_order_acquire)) {
  _running=false;uint32_t delay=2000;
  if(!_result.ok()){delay=std::max(_backoff,_result.retry_ms);_backoff=std::min<uint32_t>(_backoff*2U,900000U);}else _backoff=60000;
  _gate=now+delay;_gate_wait=true;
  if(_job_generation==_generation){
   if(_job_kind==transit::Request::Board){if(_result.ok()){_board=std::move(_result);_fetched=now;}else _board.error=_result.error;_deadline=now+(_board.error.empty()?120000:delay);}
   else if(_job_kind==transit::Request::Search)_search=std::move(_result);else _trip=std::move(_result);
   ++_revision;
  }_result={};
 }
 if(_gate_wait&&int32_t(now-_gate)>=0)_gate_wait=false;
 if(!_active||_running||!online())return;
 if(_auto&&!_pending&&!_favourites.empty()&&int32_t(now-_deadline)>=0)queue(transit::Request::Board,_favourites[_selected].id);
 if(!_pending||_gate_wait)return;
#if defined(ARDUINO)
 auto state=UpdateService::instance().state();if(state==UpdateService::State::Checking||state==UpdateService::State::Downloading||state==UpdateService::State::ReadyToReboot)return;
#endif
 _pending=false;_running=true;_job_kind=_kind;_job_argument=_argument;_job_generation=_generation;_finished.store(false,std::memory_order_relaxed);++_revision;
#if defined(ARDUINO)
 if(xTaskCreate(worker,"transit",12288,this,1,nullptr)!=pdPASS){_result.error=i18n::tr("Not enough memory for request");_finished.store(true,std::memory_order_release);}
#else
 std::thread(worker,this).detach();
#endif
}
SERVICE_REGISTER(TransitService);
