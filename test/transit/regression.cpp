#include <array>
#include <atomic>
#include <string>
#include <cassert>
#include <cmath>
#include <fstream>
#include <thread>
#include <chrono>
#include <cstdlib>
#include "services/TransitProvider.h"
#define private public
#include "services/TransitService.h"
#undef private
#include "../../src/apps/TransitApp.cpp"

static uint8_t frame[320*480*3];
static uint32_t tick;
static void capture(const char *name) {
    lv_obj_update_layout(lv_screen_active());lv_refr_now(nullptr);
    if(const char *dir=std::getenv("TRANSIT_PREVIEW_DIR")) {
        std::ofstream f(std::string(dir)+"/"+name+".ppm",std::ios::binary);
        f<<"P6\n320 480\n255\n";f.write(reinterpret_cast<char *>(frame),sizeof(frame));
    }
}

static void finish(TransitService &s) {
 for(int i=0;i<1000 && s._running;++i){std::this_thread::sleep_for(std::chrono::milliseconds(2));s.loop();}
 assert(!s._running);
}
int main(int argc,char **argv){
 using namespace transit;
 assert(timestamp("2026-10-24T14:10:00Z")==timestamp("2026-10-24T16:10:00+02:00"));
 assert(timestamp("2026-10-24T14:10:00.123Z")==timestamp("2026-10-24T14:10:00Z"));
 assert(!timestamp("2026-02-29T14:10:00Z"));assert(timestamp("2024-02-29T14:10:00Z"));
 assert(!timestamp("2026-10-24T14:10:00"));assert(!timestamp("2026-10-24T14:10:00+2x:00"));
 assert(encode("Zürich & x")=="Z%C3%BCrich%20%26%20x");
 const std::string search=R"([{"type":"STOP","id":"zh","name":"Zürich HB","country":"CH"},{"type":"PLACE","id":"ignore"}])";
 const std::string boardData=R"({"stopTimes":[{"displayName":"S8","headsign":"Winterthur","tripId":"trip","realTime":true,"place":{"departure":"2026-10-24T14:10:00Z","scheduledDeparture":"2026-10-24T14:08:00Z","track":"43"}},{"routeShortName":"IR13","tripTo":{"name":"St. Gallen"},"tripCancelled":true,"place":{"scheduledDeparture":"2026-10-24T14:20:00Z"}}]})";
 const std::string tripData=R"({"legs":[{"from":{"stopId":"zh","name":"Zürich HB","departure":"2026-10-24T14:10:00Z"},"intermediateStops":[{"stopId":"o","name":"Oerlikon","arrival":"2026-10-24T14:15:00Z"}],"to":{"stopId":"w","name":"Winterthur","arrival":"2026-10-24T14:30:00Z"}}]})";
 auto r=TransitousProvider::parse(Request::Search,search);assert(r.ok()&&r.stops.size()==1&&r.stops[0].name=="Zurich HB");
 r=TransitousProvider::parse(Request::Board,boardData);assert(r.ok()&&r.departures.size()==2);
 assert(r.departures[0].time-r.departures[0].scheduled==120&&r.departures[0].live);
 assert(r.departures[1].cancelled&&r.departures[1].destination=="St. Gallen"&&r.departures[1].platform.empty());
 assert(TransitousProvider::parse(Request::Trip,tripData).calls.size()==3);
 assert(!TransitousProvider::parse(Request::Board,"{}").ok());
 assert(!TransitousProvider::parse(Request::Search,"<html>bad</html>").ok());
 assert(!TransitousProvider::parse(Request::Trip,std::string(200000,'x')).ok());
 for(int i=1;i<argc&&i<=3;++i){std::ifstream f(argv[i]);std::string body((std::istreambuf_iterator<char>(f)),{});auto live=TransitousProvider::parse(static_cast<Request>(i-1),body);assert(live.ok());assert(!live.stops.empty()||!live.departures.empty()||!live.calls.empty());}
 const char*dir=std::getenv("GADGET_OS_DATA_DIR");assert(dir);setenv("GADGET_OS_TRANSIT_FIXTURE_DIR",dir,1);
 std::ofstream(std::string(dir)+"/search.json")<<search;
 std::ofstream(std::string(dir)+"/board.json")<<boardData;
 std::ofstream(std::string(dir)+"/trip.json")<<tripData;
 auto&s=TransitService::instance();assert(s.begin());assert(s.add({"zh","Zurich HB","CH"}));
 s.loop();finish(s);assert(s.board().departures.size()==2&&s.fetched());
 s.refresh();assert(!s._pending&&!s.notice().empty());
 s._fetched-=31000;s.refresh();s.loop();assert(s._pending&&!s._running); // Cannot bypass global spacing.
 s._gate_wait=false;s.loop();s.pause();finish(s);assert(!s._active); // Discard cancelled response.
 s.resume();s.search("Zurich");s._gate_wait=false;s.loop();finish(s);assert(s.search_result().stops.size()==1);
 assert(s.add({"zh","Zurich HB","CH"}));assert(s.favourites().size()==1);
 s.hold();s._favourites.clear();assert(s.begin());assert(s.favourites().size()==1); // Disk round-trip.
 s.select(0);s._gate_wait=false;s.loop();finish(s);assert(s.board().departures.size()==2);
 std::ofstream(std::string(dir)+"/board.json")<<"bad";
 s._fetched-=31000;s.refresh();s._gate_wait=false;s.loop();finish(s);
 assert(!s.board().error.empty()&&s.board().departures.size()==2);assert(s._gate_wait&&s._backoff==120000);
 s._board.error.clear();s._notice.clear();s._pending=false;
 for(size_t i=0;i<s._board.departures.size();++i){auto&d=s._board.departures[i];d.time=std::time(nullptr)+180+i*300;d.scheduled=d.time-120;}
 s._trip=TransitousProvider::parse(Request::Trip,tripData);
 for(size_t i=0;i<s._trip.calls.size();++i)s._trip.calls[i].time=s._board.departures[0].time+i*300;
    lv_init();lv_tick_set_cb([](){return tick;});auto *display=lv_display_create(320,480);
    lv_obj_add_flag(lv_layer_sys(),LV_OBJ_FLAG_HIDDEN);static uint8_t buffer[320*40*4];
    lv_display_set_color_format(display,LV_COLOR_FORMAT_XRGB8888);
    lv_display_set_buffers(display,buffer,nullptr,sizeof(buffer),LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(display,[](lv_display_t *d,const lv_area_t *a,uint8_t *pixels){
        for(int y=a->y1;y<=a->y2;++y)for(int x=a->x1;x<=a->x2;++x) {
            int src=((y-a->y1)*(a->x2-a->x1+1)+x-a->x1)*4,dst=(y*320+x)*3;
            frame[dst]=pixels[src+2];frame[dst+1]=pixels[src+1];frame[dst+2]=pixels[src];
        }lv_display_flush_ready(d);
    });

 transit_app_launch();capture("transit-board");
 const auto originalBoard=s._board;
 auto longName=s._board.departures[0];longName.line="33";longName.destination="Zurich Tiefenbrunnen, Bahnhof";
 s._board.departures.push_back(longName);render();capture("transit-long-destination");
 s._board=originalBoard;render();
 ctx.departure=s.board().departures[0];ctx.view=3;render();capture("transit-detail");
 stops(nullptr);capture("transit-stops");
 start_search(nullptr);capture("transit-search");
 lv_area_t keyboardArea;lv_obj_get_coords(lv_obj_get_child(ctx.body,-1),&keyboardArea);assert(keyboardArea.y1==252&&keyboardArea.y2<480);
 lv_textarea_set_text(ctx.input,"Zurich");submit(nullptr);assert(ctx.submitted);capture("transit-results");
 ctx.view=0;auto*screen=ctx.screen;back(nullptr);lv_obj_delete(screen);assert(!s._active&&!ctx.timer);
 std::puts("Transit parsing, persistence, cancellation, throttling, cache and UI tests passed");
}
