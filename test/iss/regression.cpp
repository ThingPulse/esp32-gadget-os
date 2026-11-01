#include <atomic>
#include <string>
#include <vector>
#include <cstdint>
#define private public
#include "services/IssService.h"
#undef private
#include "services/WeatherLocationStore.h"
#include "services/ConfigStoreService.h"
#include "services/FileSystemService.h"
#include "services/TransitProvider.h"
#include <cassert>
#include <thread>
#include <chrono>
#include <fstream>
#include <cstdlib>
#include <cstring>
#include "../../src/apps/IssApp.cpp"
static uint32_t ticks;
static unsigned char frame[320*480*3];
static std::string iso(int64_t t){time_t epoch=t;tm value{};gmtime_r(&epoch,&value);char b[32];strftime(b,sizeof(b),"%Y-%m-%dT%H:%M:%SZ",&value);return b;}
static std::string payload(int64_t rise,bool visible=true){
 return "{\"tle_stale\":false,\"generated_at\":\""+iso(rise-3600)+"\",\"passes\":[{\"rise\":{\"time\":\""+iso(rise)+"\",\"azimuth_deg\":247.5},\"culmination\":{\"time\":\""+iso(rise+180)+"\",\"elevation_deg\":62},\"set\":{\"time\":\""+iso(rise+420)+"\",\"azimuth_deg\":45},\"visible\":"+(visible?"true":"false")+",\"visible_start\":\""+iso(rise+30)+"\",\"visible_end\":\""+iso(rise+330)+"\"}]}";
}
static void finish(IssService &s){for(int i=0;i<500&&s.fetching();++i){std::this_thread::sleep_for(std::chrono::milliseconds(5));s.loop();}assert(!s.fetching());}
static void capture(const char*name){lv_obj_update_layout(ctx.screen);lv_refr_now(nullptr);
 if(const char *dir=std::getenv("ISS_PREVIEW_DIR")){std::ofstream f(std::string(dir)+"/"+name+".ppm",std::ios::binary);f<<"P6\n320 480\n255\n";f.write((char*)frame,sizeof(frame));}}
int main(int argc,char **argv){
 if(argc>1){std::ifstream file(argv[1]);std::string live((std::istreambuf_iterator<char>(file)),{});auto response=iss::parse(live);assert(response.error.empty()&&!response.passes.empty());}
 int64_t now=time(nullptr);auto body=payload(now+4980);auto f=iss::parse(body);
 assert(f.error.empty()&&f.passes.size()==1&&f.passes[0].visible_end-f.passes[0].visible_start==300);
 assert(iss::next(f,now,true));assert(!iss::next(f,f.passes[0].set,true));
 assert(!iss::next(iss::parse(payload(now,false)),now,true));
 assert(iss::next(iss::parse(payload(now,false)),now,false));
 assert(!iss::parse("{}").error.empty());assert(!iss::parse("oops").error.empty());
 assert(!iss::parse(std::string(50000,'x')).error.empty());
 auto missing=body;auto at=missing.find("\"azimuth_deg\":247.5");missing.replace(at,19,"\"azimuth_deg\":null");assert(!iss::parse(missing).error.empty());
 assert(iss::parse("{\"passes\":[]}").error.empty());
 auto both=f;iss::combine(both,f);assert(both.passes.size()==1);
 assert(std::string(iss::compass(247.5))=="WSW"&&std::string(iss::compass(360))=="N");
 assert(iss::duration(0)=="0m 00s"&&iss::duration(4980)=="1h 23m");
 auto &s=IssService::instance();const char*dir=getenv("GADGET_OS_DATA_DIR");assert(dir);setenv("GADGET_OS_ISS_FIXTURE_DIR",dir,1);
 std::ofstream(std::string(dir)+"/all.json")<<body;std::ofstream(std::string(dir)+"/visible.json")<<body;
 WeatherLocation zurich;zurich.name="Zurich";zurich.latitude=47.37;zurich.longitude=8.54;
 assert(WeatherLocationStore::save({zurich},0));s.resume();assert(s.configured()&&s._pending);s.loop();finish(s);
 assert(s.error().empty()&&s.forecast().passes.size()==1&&s.fetched());
 IssService reloaded;reloaded._key=s._key;reloaded.load_cache();assert(reloaded.forecast().passes.size()==1&&reloaded.fetched());
 s.refresh();s.loop();assert(!s.fetching()&&s._pending); // manual refresh honors global cooldown
 s._gate=false;s.loop();s.pause();finish(s);assert(!s._active);
 // Invalid response retains the last good predictions and backs off.
 std::ofstream(std::string(dir)+"/all.json")<<"bad";s.resume();s._gate=false;s.refresh();s.loop();finish(s);
 assert(!s.error().empty()&&s.forecast().passes.size()==1&&s._backoff==120000&&s._gate);
 std::ofstream(std::string(dir)+"/all.json")<<body;
 s._gate=false;s.refresh();s.loop();WeatherLocation other=zurich;other.latitude=48;assert(WeatherLocationStore::save({other},0));s.resume();s.pause();finish(s);
 assert(s.forecast().passes.empty()); // old location worker must not repopulate new location
 assert(WeatherLocationStore::save({zurich},0));s.resume();s.pause();
 s._forecast=f;s._fetched=now;s._error.clear();s._pending=false;
 lv_init();lv_tick_set_cb([](){return ticks;});auto*d=lv_display_create(320,480);i18n::apply_fonts(d);lv_obj_add_flag(lv_layer_sys(),LV_OBJ_FLAG_HIDDEN);
 static unsigned char buffer[320*40*4];lv_display_set_color_format(d,LV_COLOR_FORMAT_XRGB8888);lv_display_set_buffers(d,buffer,nullptr,sizeof(buffer),LV_DISPLAY_RENDER_MODE_PARTIAL);
 lv_display_set_flush_cb(d,[](lv_display_t*display,const lv_area_t*a,uint8_t*pixels){
  for(int y=a->y1;y<=a->y2;++y)for(int x=a->x1;x<=a->x2;++x){int src=((y-a->y1)*(a->x2-a->x1+1)+x-a->x1)*4,dst=(y*320+x)*3;frame[dst]=pixels[src+2];frame[dst+1]=pixels[src+1];frame[dst+2]=pixels[src];}lv_display_flush_ready(display);});
 auto*home=lv_obj_create(nullptr);lv_screen_load(home);iss_app_launch();capture("next");assert(ctx.shown.rise==f.passes[0].rise);
 s._forecast=iss::parse(payload(now-120));++s._revision;render();capture("active");assert(ctx.shown.rise<=now&&ctx.shown.set>now);
 // Visibility can start after rise or end before set; retain pass until set.
 s._forecast.passes[0].visible_start=now+20;s._forecast.passes[0].visible_end=now+200;++s._revision;render();capture("visibility-later");
 s._forecast.passes[0].visible_start=now-80;s._forecast.passes[0].visible_end=now-1;++s._revision;render();assert(selected_pass(now));capture("visibility-ended");
 s._forecast=f;iss::combine(s._forecast,iss::parse(payload(now+10000,false)));iss::combine(s._forecast,iss::parse(payload(now+20000)));
 ctx.passes=true;ctx.revision=~0U;render();capture("passes");auto*list=lv_obj_get_child(ctx.body,3);assert(lv_obj_get_child_count(list)==2);
 auto *first=lv_obj_get_child(list,0);render();assert(first==lv_obj_get_child(list,0)); // timer doesn't destroy scrolling list
 ctx.visible_only=false;ctx.revision=~0U;render();capture("all");list=lv_obj_get_child(ctx.body,3);assert(lv_obj_get_child_count(list)==3);
 ctx.passes=false;ctx.selected=s._forecast.passes[1].rise;render();assert(!ctx.shown.visible);capture("not-visible");
 s._forecast={};++s._revision;render();capture("empty");
 s._key.clear();++s._revision;render();capture("location");
 s._key="47.370000,8.540000";s._forecast=f;s._forecast.stale=true;ctx.selected=0;ctx.revision=~0U;render();capture("stale");
 i18n::set_language("de");close(nullptr);ticks+=100;lv_timer_handler();iss_app_launch();capture("german");
 lv_obj_send_event(lv_obj_get_child(lv_obj_get_child(ctx.screen,0),0),LV_EVENT_CLICKED,nullptr);
 ticks+=100;lv_timer_handler();assert(!s._active&&lv_screen_active()==home);
 puts("ISS parser, visibility windows, cache, scheduling, location changes and UI regressions passed");
}
