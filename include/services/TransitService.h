#pragma once
#include "ServiceRegistry.h"
#include "services/TransitProvider.h"
#include <atomic>
class TransitService : public SystemService {
public:
 static TransitService &instance(); const char *name() const override{return "Transit";}
 bool begin() override;void loop() override;
 void resume();void pause(){_active=false;_pending=false;++_generation;}
 void hold(){_auto=false;_pending=false;++_generation;}
 void select(size_t index);void search(const std::string &query);void trip(const std::string &id);
 void refresh();bool add(const transit::Stop &stop);bool remove(size_t index);
 const std::vector<transit::Stop>& favourites()const{return _favourites;}
 size_t selected()const{return _selected;}
 const transit::Result &board()const{return _board;}
 const transit::Result &search_result()const{return _search;}
 const transit::Result &details()const{return _trip;}
 bool busy()const{return _running||_pending;}uint32_t revision()const{return _revision;}
 uint32_t fetched()const{return _fetched;}static uint32_t now_ms();
 bool online() const;const std::string &notice()const{return _notice;}
private:
 static void worker(void *self);void queue(transit::Request,const std::string &);bool save(const std::vector<transit::Stop>&);
 transit::TransitousProvider _provider;
 std::vector<transit::Stop> _favourites;
 transit::Result _board,_search,_trip,_result;
 size_t _selected=0;uint32_t _generation=0,_job_generation=0,_revision=0,_fetched=0,_deadline=0,_gate=0,_backoff=60000;
 bool _active=false,_running=false,_pending=false,_auto=false,_gate_wait=false;
 std::atomic<bool> _finished{false};
 transit::Request _kind=transit::Request::Board,_job_kind=transit::Request::Board;
 std::string _argument,_job_argument,_notice;
};
