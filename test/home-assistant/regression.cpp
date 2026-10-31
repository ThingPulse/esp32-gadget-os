#include <cassert>
#include <chrono>
#include <thread>
#include <fstream>
#include <cstdlib>
#include <atomic>
#include "services/HomeAssistantModel.h"
#include "services/HomeAssistantHttp.h"
#include "services/HomeAssistantEntityStream.h"
#include "services/TransitService.h"
#define private public
#include "services/HomeAssistantService.h"
#undef private
#include "../../src/apps/HomeAssistantApp.cpp"
static void finish(HomeAssistantService&s){for(int i=0;i<1500&&s.busy();++i){std::this_thread::sleep_for(std::chrono::milliseconds(10));s.loop();}assert(!s.busy());}
static uint8_t frame[320*480*3];static uint32_t tick;
static void capture(const char*name){lv_obj_update_layout(lv_screen_active());lv_refr_now(nullptr);if(const char*dir=getenv("HOME_PREVIEW_DIR")){std::ofstream f(std::string(dir)+"/"+name+".ppm",std::ios::binary);f<<"P6\n320 480\n255\n";f.write(reinterpret_cast<char*>(frame),sizeof frame);}}
int main(int argc,char**argv){
 using namespace ha;assert(argc>=2);std::string url=argv[1],error;
 assert(valid_entity("light.sofa"));assert(!valid_entity("lock.front_door"));assert(!valid_entity("light.sofa/../../"));assert(!valid_entity("light.sofa.x"));assert(!valid_entity("sensor."));
 assert(valid_url(url));assert(!valid_url("https://user:pass@host"));assert(!valid_url("file:///tmp/x"));assert(!valid_url("https://host/path"));assert(!valid_url("http://host\r\nHeader:x"));
 Config c;c.url=url;c.token="test-token";c.pages={{"Living room",{{"light.sofa","Dining Table Pendant","light"},{"switch.ceiling","Ceiling","power"},{"sensor.temperature","Temperature","temperature"},{"sensor.humidity","Humidity","water"},{"scene.movie","Movie night","play"},{"script.all_off","All off","power"}}},{"Energy",{{"sensor.solar","Solar","energy"}}}};
 auto raw=config_json(c,true);Config parsed;if(!parse_config(raw,parsed,error)){std::fprintf(stderr,"Config error: %s; %s\n",error.c_str(),raw.c_str());std::abort();}assert(parsed.pages.size()==2&&parsed.pages[0].tiles.size()==6);
 assert(config_json(c).find("test-token")==std::string::npos);assert(parse_config(config_json(c),parsed,error,&c)&&parsed.token==c.token);
 auto bad=c;bad.url="http://other";assert(!parse_config(config_json(bad),parsed,error,&c));bad=c;bad.pages[0].tiles.push_back(bad.pages[0].tiles[0]);assert(!parse_config(config_json(bad,true),parsed,error));bad=c;bad.pages[0].tiles[0].id="lock.front";assert(!parse_config(config_json(bad,true),parsed,error));
 Entity e;e.id="light.sofa";e.state="on";e.dimmable=true;e.warmth=true;std::string path,body;
 assert(command(c,e,"toggle",0,path,body)&&path=="/api/services/light/turn_off");assert(command(c,e,"brightness",65,path,body)&&body.find("65")!=std::string::npos);assert(!command(c,e,"brightness",101,path,body));assert(!command(c,e,"warmth",10,path,body));assert(!command(c,e,"delete",0,path,body));e.state="unavailable";assert(!command(c,e,"toggle",0,path,body));e.state="on";e.id="light.unselected";assert(!command(c,e,"toggle",0,path,body));
 // Only color-capable lights accept RGB commands, including HA-converted modes.
 for(const char*mode:{"hs","xy","rgb","rgbw","rgbww","color_temp","brightness","onoff"}){
  Entity light;assert(parse_entity(std::string(R"({"entity_id":"light.sofa","state":"on","attributes":{"supported_color_modes":[")")+mode+R"("]}})",light,error));
  bool supported=std::string(mode)!="color_temp"&&std::string(mode)!="brightness"&&std::string(mode)!="onoff";
  assert(light.color==supported);assert(command(c,light,"color",0xff0080,path,body)==supported);
  if(supported){assert(path=="/api/services/light/turn_on"&&body.find("\"rgb_color\":[255,0,128]")!=std::string::npos);assert(!command(c,light,"color",-1,path,body));assert(!command(c,light,"color",0x1000000,path,body));light.state="unavailable";assert(!command(c,light,"color",0xff0000,path,body));}
 }
 Entity invalidEntity;assert(!parse_entity("[]",invalidEntity,error));assert(!parse_entity(std::string(1048577,'x'),invalidEntity,error));
 // Frame across arbitrary chunk boundaries, including escaped quotes/braces.
 const std::string framed=R"([{"entity_id":"sensor.test","state":"ok","attributes":{"friendly_name":"Test \" { [","ignored":{"x":[1,2]}}},{"entity_id":"lock.door","state":"locked"}])";
 for(size_t chunk:{size_t(1),size_t(7),size_t(512)}){EntityStream stream;for(size_t i=0;i<framed.size();i+=chunk)assert(stream.feed(framed.data()+i,std::min(chunk,framed.size()-i)));assert(stream.finish()&&stream.items.size()==1&&stream.matches==1);}
 for(const std::string malformed:{"[{", "[{},]", "[]x", "{}", "[true]"}){EntityStream stream;bool fed=stream.feed(malformed.data(),malformed.size());assert(!fed||!stream.finish());}
 std::string largeResponse;EntityStream first;assert(request(c,"/api/states?large","",largeResponse,error,&first));assert(first.matches==607&&first.items.size()==256);
 EntityStream chunked;assert(request(c,"/api/states?chunked","",largeResponse,error,&chunked));assert(chunked.matches==607&&chunked.items.size()==256);
 EntityStream next(256);assert(request(c,"/api/states?large","",largeResponse,error,&next));assert(next.matches==607&&next.items.size()==256&&next.items[0].id!=first.items[0].id);
 EntityStream search(0,"extra_599","sensor");assert(request(c,"/api/states?large","",largeResponse,error,&search));assert(search.matches==1&&search.items[0].id=="sensor.extra_599");
 auto&s=HomeAssistantService::instance();assert(s.begin());assert(s.configure(raw,error));assert(s.begin());assert(s.config().pages[0].tiles.size()==6&&s.config().token=="test-token");assert(s.discover());finish(s);assert(s.connected()&&s.entities().size()==7);assert(s.entity("light.sofa")->dimmable&&s.entity("light.sofa")->warmth);
 assert(s.act("light.sofa","brightness",65));finish(s);assert(s.notice().empty());assert(s.act("light.sofa","color",0xff0080));finish(s);assert(!s.act("sensor.temperature","toggle"));
 assert(s.open_setup());auto setupUrl=s.setup_url();auto key=setupUrl.substr(setupUrl.find('#')+1);int status;
 assert(s.setup_request("GET","/api/config","bad","",status).find("error")!=std::string::npos&&status==403);
 auto publicConfig=s.setup_request("GET","/api/config",key,"",status);assert(status==200&&publicConfig.find("test-token")==std::string::npos);
 s.close_setup();s.setup_request("GET","/api/config",key,"",status);assert(status==403);
 if(argc>2&&std::string(argv[2])=="--serve"){
  assert(s.open_setup());std::puts(s.setup_url().c_str());fflush(stdout);for(int i=0;i<18000;++i){s.loop();std::this_thread::sleep_for(std::chrono::milliseconds(10));}return 0;
 }
 // Exercise the renderer at physical resolution in both languages.
 lv_init();lv_tick_set_cb([](){return tick;});auto*display=lv_display_create(320,480);static uint8_t buffer[320*40*4];lv_obj_add_flag(lv_layer_sys(),LV_OBJ_FLAG_HIDDEN);lv_display_set_color_format(display,LV_COLOR_FORMAT_XRGB8888);lv_display_set_buffers(display,buffer,nullptr,sizeof buffer,LV_DISPLAY_RENDER_MODE_PARTIAL);
 lv_display_set_flush_cb(display,[](lv_display_t*d,const lv_area_t*a,uint8_t*p){for(int y=a->y1;y<=a->y2;++y)for(int x=a->x1;x<=a->x2;++x){int src=((y-a->y1)*(a->x2-a->x1+1)+x-a->x1)*4,dst=(y*320+x)*3;frame[dst]=p[src+2];frame[dst+1]=p[src+1];frame[dst+2]=p[src];}lv_display_flush_ready(d);});
 for(auto language:{"en","de"}){
  i18n::set_language(language);i18n::apply_fonts(display);home_assistant_app_launch();capture((std::string("home-")+language).c_str());assert(ctx.page==0);
  auto*originalTile=ctx.tiles[0].button;auto*originalValue=ctx.tiles[0].value;
  s._running=true;++s._revision;refresh();assert(ctx.tiles[0].button==originalTile&&!lv_obj_has_state(originalTile,LV_STATE_DISABLED));
  s._running=false;++s._revision;refresh();assert(ctx.tiles[0].button==originalTile&&ctx.tiles[0].value==originalValue);
  auto oldState=s._entities[0].state;s._entities[0].state="off";++s._revision;refresh();assert(ctx.tiles[0].button==originalTile&&std::string(lv_label_get_text(originalValue))==i18n::tr("Off"));s._entities[0].state=oldState;refresh();
  s._online=false;refresh();assert(lv_obj_has_state(originalTile,LV_STATE_DISABLED));s._online=true;refresh();assert(!lv_obj_has_state(originalTile,LV_STATE_DISABLED));
  lv_obj_send_event(originalTile,LV_EVENT_CLICKED,nullptr);assert(ctx.view==0&&s.busy()&&s._job_path=="/api/services/light/turn_off");assert(std::string(lv_label_get_text(originalValue))==i18n::tr("Off")&&!lv_obj_has_flag(ctx.tiles[0].spinner,LV_OBJ_FLAG_HIDDEN));capture((std::string("home-pending-")+language).c_str());finish(s);
  refresh();assert(s.pending("light.sofa")&&std::string(s.expected_state("light.sofa"))=="off");assert(!lv_obj_has_flag(ctx.tiles[0].spinner,LV_OBJ_FLAG_HIDDEN));assert(!s.act("light.sofa","toggle"));
  // A stale read must not replace the optimistic display or clear pending.
  s._result=s._entities;s._job=0;s._running=true;s._finished=true;s.loop();assert(s.pending("light.sofa"));refresh();assert(std::string(lv_label_get_text(originalValue))==i18n::tr("Off"));
  s._result=s._entities;s._result[0].state="off";s._job=0;s._running=true;s._finished=true;s.loop();refresh();assert(!s.pending("light.sofa")&&lv_obj_has_flag(ctx.tiles[0].spinner,LV_OBJ_FLAG_HIDDEN));
  s._entities[0].state="off";lv_obj_send_event(originalTile,LV_EVENT_CLICKED,nullptr);assert(ctx.view==0&&s.busy()&&s._job_path=="/api/services/light/turn_on");finish(s);
  s._result=s._entities;s._result[0].state=oldState;s._job=0;s._running=true;s._finished=true;s.loop();assert(!s.pending("light.sofa"));
  auto*chevron=lv_obj_get_child(originalTile,0);assert(lv_obj_get_width(chevron)==44&&!lv_obj_has_flag(chevron,LV_OBJ_FLAG_EVENT_BUBBLE));
  lv_obj_send_event(chevron,LV_EVENT_CLICKED,nullptr);assert(ctx.view==1&&ctx.detail=="light.sofa"&&!s.busy());capture((std::string("home-light-")+language).c_str());
  lv_obj_scroll_to_y(ctx.body,1000,LV_ANIM_OFF);capture((std::string("home-color-")+language).c_str());
  auto*swatch=lv_obj_get_child(ctx.body,-11);lv_obj_send_event(swatch,LV_EVENT_CLICKED,nullptr);assert(s.busy()&&s._job_body.find("rgb_color")!=std::string::npos);finish(s);refresh();
  auto*detailChild=lv_obj_get_child(ctx.body,0);s._running=true;++s._revision;refresh();assert(lv_obj_get_child(ctx.body,0)==detailChild);s._running=false;
  setup(nullptr);assert(ctx.view==2);capture((std::string("home-setup-")+language).c_str());
  auto*screen=ctx.screen;ctx.view=0;back(nullptr);lv_obj_delete(screen);assert(!s._active&&!s.setup_open()&&!ctx.timer);
 }
 // Queue behind an in-flight poll; other controls remain usable, duplicates do not.
 s.resume();s._running=true;assert(s.act("light.sofa","toggle"));assert(!s.act("light.sofa","toggle"));assert(s.act("switch.ceiling","toggle"));assert(s._actions.size()==2);
 s._job=0;s._job_generation=s._generation;s._result=s._entities;s._result_error.clear();s._finished=true;s.loop();finish(s);
 assert(s.pending("light.sofa")&&!s.pending("switch.ceiling"));
 // Timeout restores the last confirmed value, even when refreshes never match.
 s._actions[0].deadline=TransitService::now_ms();s.loop();assert(!s.pending("light.sofa")&&s.notice()=="Home action not confirmed");assert(s.entity("light.sofa")->state=="on");
 // Failed command also rolls back; request failure cannot leave a stuck spinner.
 s._running=true;assert(s.act("light.sofa","toggle"));s._actions[0].sent=true;s._job_action=s._actions[0].serial;s._job=2;s._job_generation=s._generation;s._result_error="Home Assistant access denied";s._finished=true;s.loop();assert(!s.pending("light.sofa")&&s.entity("light.sofa")->state=="on");
 s._notice_until=TransitService::now_ms();s.loop();assert(s.notice().empty());s.pause();
 // A late response for a timed-out action must not acknowledge its replacement.
 s._online=true;s._running=true;s._finished=false;assert(s.act("light.sofa","toggle"));auto expired=s._actions[0].serial;
 s._actions[0].deadline=TransitService::now_ms();s.loop();assert(!s.pending("light.sofa"));assert(s.act("light.sofa","toggle"));
 s._job=2;s._job_action=expired;s._job_generation=s._generation;s._result_error.clear();s._finished=true;s.loop();assert(s.pending("light.sofa")&&!s._actions[0].accepted);finish(s);s.pause();
 // An auth failure disables commands, and closing cancels results from an old generation.
 c.token="wrong";assert(s.configure(config_json(c,true),error));assert(s.discover());finish(s);assert(!s.connected()&&s.notice()=="Home Assistant access denied");assert(!s.act("light.sofa","toggle"));
 c.token="test-token";assert(s.configure(config_json(c,true),error));assert(s.discover());s.pause();finish(s);assert(s.entities().empty());
 std::puts("Home Assistant validation, commands, auth, persistence, cancellation, setup and UI tests passed");
}
