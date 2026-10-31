#pragma once
#include "ServiceRegistry.h"
#include "services/HomeAssistantModel.h"
#include <atomic>
class HomeAssistantService:public SystemService {
public:
 static HomeAssistantService&instance();
 const char*name()const override{return "HomeAssistant";}
 bool begin()override;void loop()override;
 void resume();void pause();
 bool configure(const std::string&,std::string&);bool discover(size_t offset=0,const std::string&query="",const std::string&kind="");
 bool act(const std::string&id,const std::string&action,int value=0);
 const ha::Config&config()const{return _config;}
 const std::vector<ha::Entity>&entities()const{return _entities;}
 const ha::Entity*entity(const std::string&id)const;
 bool pending(const std::string&id)const;
 const char* expected_state(const std::string&id)const;
 int expected_value(const std::string&id,const std::string&action,int fallback)const;
 bool busy()const{return _running;}
 bool connected()const;
 uint32_t revision()const{return _revision;}
 uint32_t layout_revision()const{return _generation;}
 const std::string&notice()const{return _notice;}
 bool open_setup();void close_setup();std::string setup_url()const;
 bool setup_open()const;
 // Called only on the main loop by the bounded setup HTTP server.
 std::string setup_request(const std::string&method,const std::string&path,const std::string&key,const std::string&body,int&status);
private:
 static void worker(void*);bool start(int kind,const std::string&path="",const std::string&body="");
 struct Action{std::string id,path,body,expected;uint32_t deadline,serial;bool sent=false,accepted=false;std::string control;int value=0;};
 std::vector<Action> _actions;
 uint32_t _action_serial=0,_job_action=0;
 uint32_t _notice_until=0;
 void dispatch();
 ha::Config _config,_job_config;
 std::vector<ha::Entity> _entities,_result;
 std::string _notice,_result_error,_job_path,_job_body,_setup_key;
 bool _active=false,_running=false,_online=false;int _job=0;
 uint32_t _revision=0,_generation=0,_job_generation=0,_next=0,_seen=0,_setup_until=0;
 size_t _discovery_total=0,_discovery_offset=0,_result_total=0,_job_offset=0;
 std::atomic<bool> _finished{false};
};
