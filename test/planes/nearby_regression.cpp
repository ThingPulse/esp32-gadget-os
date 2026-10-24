#include <string>
#include <unordered_map>
#include <cassert>
#include <cstring>
#include <fstream>
#include <cmath>
#include <cstdlib>
#include <algorithm>
#include <iterator>
#include <limits>
#include "ui/PlaneAltitudeColor.h"
#define private public
#include "services/PlanesService.h"
#undef private
#include "../../src/apps/PlanesApp.cpp"
static unsigned char frame[320*480*3];
static uint32_t tick_ms;
static lv_indev_data_t input_data;
static PlaneState sample(const char *icao,const char *call,float distance,float bearing,float heading) {
    PlaneState p; p.valid=true;p.icao24=icao;p.callsign=call;p.distance_km=distance;p.bearing_deg=bearing;
    p.heading_valid=true;p.heading_deg=heading;p.altitude_m=3185;p.velocity_ms=150;p.aircraft_type="E190";
    return p;
}
static void capture(const char *name) {
    lv_obj_update_layout(lv_screen_active());lv_refr_now(nullptr);
    if (const char *directory=std::getenv("NEARBY_PREVIEW_DIR")) {
        std::ofstream f(std::string(directory)+"/"+name+".ppm",std::ios::binary);
        f<<"P6\n320 480\n255\n";f.write((char *)frame,sizeof(frame));
    }
}
int main() {
    using PlaneAltitudeColor::band;
    assert(band(-100)==1 && band(0)==1 && band(1999)==1 && band(2000)==2);
    assert(band(3999)==2 && band(4000)==3 && band(6000)==4 && band(9000)==5);
    assert(band(std::numeric_limits<float>::quiet_NaN())==0);
    assert(band(std::numeric_limits<float>::infinity())==0);
    lv_init();lv_tick_set_cb([](){return tick_ms;});
    auto *display=lv_display_create(320,480);
    lv_obj_add_flag(lv_layer_sys(),LV_OBJ_FLAG_HIDDEN);
    static unsigned char buffer[320*40*4];
    lv_display_set_color_format(display,LV_COLOR_FORMAT_XRGB8888);
    lv_display_set_buffers(display,buffer,nullptr,sizeof(buffer),LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(display,[](lv_display_t *d,const lv_area_t *a,uint8_t *pixels){
        for(int y=a->y1;y<=a->y2;++y)for(int x=a->x1;x<=a->x2;++x){
            const int src=((y-a->y1)*(a->x2-a->x1+1)+x-a->x1)*4,dst=(y*320+x)*3;
            frame[dst]=pixels[src+2];frame[dst+1]=pixels[src+1];frame[dst+2]=pixels[src];
        }lv_display_flush_ready(d);
    });
    auto &s=PlanesService::instance();
    s._configured=true;s._home_lat=47.4;s._home_lon=8.5;s._home_range_km=50;
    s._center_lat=47.4;s._center_lon=8.5;s._range_km=50;
    planes_app_launch();
    assert(app_ctx.nearby.root && s.range_km()==25 && s._airborne_only);
    PlaneState planes[]={sample("a001","SWR3WZ",4,45,225),sample("a002","CFG314",4.2,280,90),
        sample("a003","SWR4B",7.8,145,315),sample("a004","BAW12",7.1,355,0),sample("a005","GROUND",1,0,0)};
    planes[4].on_ground=true;
    planes[0].altitude_m=1500;planes[1].altitude_m=3500;
    planes[2].altitude_m=4500;planes[3].altitude_m=7500;
    planes[0].route="ZRH->LHR";
    planes[0].origin_airport_name="Zurich Airport";
    planes[0].destination_airport_name="London Heathrow Airport";
    s.publish_result(planes,5,"Test");planes_app_update_ui();capture("nearby");
    assert(app_ctx.nearby.count==4 && app_ctx.nearby.selected_icao=="a001");
    assert(std::strstr(lv_label_get_text(app_ctx.nearby.look),"northeast"));
    assert(std::strstr(lv_label_get_text(app_ctx.nearby.distance),"Approaching"));
    auto &v=app_ctx.nearby;
    static_assert(sizeof(v.trails) == 3840, "Altitude bands add only 400 bytes to the trail history");
    auto trail = [&]() -> const NearbyPlanesView::AircraftTrail & {
        for (const auto &t : v.trails) if (v.selected_icao == t.icao) return t;
        assert(false);return v.trails[0];
    };
    assert(std::strstr(lv_label_get_text(v.route),"ZRH"));
    assert(std::strstr(lv_label_get_text(v.route),"LHR"));
    lv_obj_send_event(v.card,LV_EVENT_CLICKED,nullptr);
    assert(v.dialog && v.dialog_icao=="a001");
    assert(std::strstr(lv_label_get_text(v.dialog_airports[1]),"London Heathrow Airport"));
    capture("details");
    const auto original = planes[0];
    planes[0].callsign="SWR2EG";planes[0].aircraft_type="A21N";
    planes[0].route="ZRH->PMI";planes[0].destination_airport_name="Palma de Mallorca Airport";
    planes[0].altitude_m=1509;planes[0].velocity_ms=286/3.6f;
    planes[0].distance_km=11.2f;planes[0].bearing_deg=315;
    planes[0].registration="HB-JPD";planes[0].squawk="1000";
    s.publish_result(planes,5,"Test");planes_app_update_ui();
    assert(std::string(lv_label_get_text(v.dialog_type))=="Airbus A321neo");
    assert(std::string(lv_label_get_text(v.dialog_stats[3]))=="Northwest");
    capture("details-redesign");
    lv_obj_update_layout(v.dialog);
    lv_area_t stats_before, stats_after, ids, close;
    lv_obj_get_coords(v.dialog_stats[0], &stats_before);
    planes[0].origin_airport_name="Thessaloniki Macedonia International Airport";
    planes[0].destination_airport_name="Rome-Fiumicino Leonardo da Vinci International Airport";
    s.publish_result(planes,5,"Test");planes_app_update_ui();
    capture("details-long-airports");
    lv_obj_get_coords(v.dialog_stats[0], &stats_after);
    lv_obj_get_coords(v.dialog_hex, &ids);lv_obj_get_coords(v.dialog_close, &close);
    assert(stats_before.y1 == stats_after.y1 && ids.y2 < close.y1);
    assert(std::strstr(lv_label_get_text(v.dialog_airports[0]), "..."));
    assert(std::strstr(lv_label_get_text(v.dialog_airports[1]), "..."));
    // Returning to short names restores their complete labels.
    planes[0].origin_airport_name="Zurich Airport";
    planes[0].destination_airport_name="Palma de Mallorca Airport";
    s.publish_result(planes,5,"Test");planes_app_update_ui();
    assert(std::string(lv_label_get_text(v.dialog_airports[0])) == "Zurich Airport");
    s._metric=false;planes_app_update_ui();
    assert(std::strstr(lv_label_get_text(v.dialog_stats[0]),"ft"));
    assert(std::strstr(lv_label_get_text(v.dialog_stats[1]),"kt"));
    capture("details-imperial");
    s._metric=true;planes[0]=original;s.publish_result(planes,5,"Test");planes_app_update_ui();
    lv_obj_send_event(v.dialog_close,LV_EVENT_CLICKED,nullptr);
    tick_ms+=10;lv_timer_handler();assert(!v.dialog);
    // Footer navigation reaches every aircraft and wraps in distance order.
    for(const char *id : {"a002","a004","a003","a001"}) {
        lv_obj_send_event(v.next,LV_EVENT_CLICKED,nullptr);assert(v.selected_icao==id);assert(s._priority_icao==id);
    }
    lv_obj_send_event(v.previous,LV_EVENT_CLICKED,nullptr);assert(v.selected_icao=="a003");
    lv_obj_send_event(v.next,LV_EVENT_CLICKED,nullptr);assert(v.selected_icao=="a001");
    auto *input=lv_indev_create();lv_indev_set_type(input,LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(input,[](lv_indev_t *,lv_indev_data_t *data){*data=input_data;});
    input_data.point={180,230};input_data.state=LV_INDEV_STATE_PRESSED;lv_indev_read(input);
    input_data.state=LV_INDEV_STATE_RELEASED;lv_indev_read(input);
    assert(app_ctx.nearby.selected_icao=="a003");
    app_ctx.nearby.selected_icao="a001";nearby_planes_update(app_ctx.nearby);
    // Footer selects, the featured card opens details and preserves selection.
    lv_obj_send_event(app_ctx.nearby.next,LV_EVENT_CLICKED,nullptr);
    assert(app_ctx.nearby.selected_icao=="a002");
    lv_obj_send_event(app_ctx.nearby.card,LV_EVENT_CLICKED,nullptr);
    assert(v.dialog && v.dialog_icao=="a002");
    assert(lv_obj_has_flag(v.route,LV_OBJ_FLAG_HIDDEN));
    assert(std::strstr(lv_label_get_text(v.dialog_route_missing),"Route unavailable"));
    assert(!lv_obj_has_flag(v.dialog_route_missing,LV_OBJ_FLAG_HIDDEN));
    assert(lv_obj_has_flag(v.dialog_route,LV_OBJ_FLAG_HIDDEN));
    capture("details-no-route");
    // Enrichment arriving during an open dialog is reflected without reopening it.
    planes[1].route="EDDF->EGLL";
    s.publish_result(planes,5,"Test");planes_app_update_ui();
    assert(std::strstr(lv_label_get_text(v.dialog_codes[0]),"EDDF"));
    lv_obj_delete(v.dialog);assert(!v.dialog);
    std::swap(planes[0],planes[3]);s.publish_result(planes,5,"Test");planes_app_update_ui();
    assert(app_ctx.nearby.selected_icao=="a002"); // Not tied to provider ordering.
    assert(trail().count==1 && std::string(trail().icao)=="a002");
    for (int i=0;i<5;++i) planes_app_update_ui();
    assert(trail().count==1); // Redraws and unchanged reports don't fill the trail.
    for (int i=0;i<22;++i) {
        planes[1].altitude_m=500+i*500;
        planes[1].distance_km=8.5f-i*.2f;
        planes[1].bearing_deg=240+i*4;
        planes[3].distance_km=4.0f+i*.05f; // a001 is not selected.
        s.publish_result(planes,5,"Test");planes_app_update_ui();
    }
    assert(trail().count==20);
    assert(trail().altitude_band(0)==1 && trail().altitude_band(1)==2);
    assert(trail().altitude_band(19)==5);
    const double oldest_angle=248*3.14159265358979323846/180;
    assert(std::abs(trail().point(0).east_km-8.1*std::sin(oldest_angle))<1e-5);
    capture("trail");
    const auto history=trail();
    app_ctx.nearby.radius_km=5;nearby_planes_update(app_ctx.nearby);
    assert(trail().count==20);
    assert(trail().point(0).east_km==history.point(0).east_km);
    capture("trail-zoom"); // Older positions outside 5 km are clipped to the radar circle.
    app_ctx.nearby.radius_km=10;nearby_planes_update(app_ctx.nearby);
    app_ctx.nearby.selected_icao="a001";nearby_planes_update(app_ctx.nearby);
    assert(trail().count==20 && std::string(trail().icao)=="a001");
    app_ctx.nearby.selected_icao="a002";nearby_planes_update(app_ctx.nearby);
    assert(trail().count==20); // Re-selecting preserves the existing history.

    // Render the actual firmware UI with a fifth aircraft and a climbing trail.
    PlaneState showcase[5];std::copy(std::begin(planes),std::end(planes),std::begin(showcase));
    showcase[4]=sample("a005","KLM87",7.5,80,250);showcase[4].altitude_m=3500;
    s.publish_result(showcase,5,"Test");planes_app_update_ui();capture("altitude-colors");
    const auto newest_band=trail().altitude_band(19);
    s._metric=false;planes_app_update_ui();
    assert(trail().altitude_band(19)==newest_band);capture("altitude-colors-imperial");
    s._metric=true;
    planes[1].altitude_m=std::numeric_limits<float>::quiet_NaN();
    s.publish_result(planes,5,"Test");planes_app_update_ui();
    assert(trail().count==20 && trail().altitude_band(19)==0);
    assert(trail().altitude_band(18)==5); // Older samples retain their own altitude.
    assert(std::strstr(lv_label_get_text(v.type),"Altitude unknown"));
    lv_obj_send_event(v.card,LV_EVENT_CLICKED,nullptr);
    assert(std::string(lv_label_get_text(v.dialog_stats[0]))=="Unknown");
    lv_obj_delete(v.dialog);
    planes[1].altitude_m=11000;s.publish_result(planes,5,"Test");planes_app_update_ui();

    s._metric=false;planes_app_update_ui();
    assert(std::strstr(lv_label_get_text(app_ctx.nearby.distance),"nm"));
    assert(std::strstr(lv_label_get_text(app_ctx.nearby.type),"ft"));capture("imperial");
    s._last_error="Offline";nearby_planes_tick(app_ctx.nearby);
    assert(std::strstr(lv_label_get_text(app_ctx.nearby.status),"Saved data"));
    lv_obj_send_event(v.card,LV_EVENT_CLICKED,nullptr);assert(v.dialog);
    s._metric=true;s.publish_result(nullptr,0,"Test");planes_app_update_ui();capture("empty");
    assert(app_ctx.nearby.count==0 && lv_obj_has_state(app_ctx.nearby.card,LV_STATE_DISABLED));
    assert(lv_obj_has_state(v.previous,LV_STATE_DISABLED) && lv_obj_has_state(v.next,LV_STATE_DISABLED));
    assert(std::strstr(lv_label_get_text(v.dialog_body),"no longer"));
    lv_obj_delete(v.dialog);
    planes[0].route="N/A";s.publish_result(planes,1,"Test");planes_app_update_ui();
    assert(lv_obj_has_flag(v.route,LV_OBJ_FLAG_HIDDEN));
    assert(lv_obj_has_state(v.next,LV_STATE_DISABLED));
    planes[0].route="ZRH->";s.publish_result(planes,1,"Test");planes_app_update_ui();
    assert(lv_obj_has_flag(v.route,LV_OBJ_FLAG_HIDDEN));
    lv_obj_send_event(v.card,LV_EVENT_CLICKED,nullptr);assert(v.dialog);
    // Fill every slot, reorder results, then replace a departed aircraft.
    PlaneState fleet[PlanesService::MAX_PLANES];
    for (int i=0;i<PlanesService::MAX_PLANES;++i)
        fleet[i]=sample(("b"+std::to_string(100+i)).c_str(),"TEST",2,30,90);
    s.publish_result(fleet,PlanesService::MAX_PLANES,"Test");planes_app_update_ui();
    for (auto &p : fleet) p.distance_km=3;
    s.publish_result(fleet,PlanesService::MAX_PLANES,"Test");planes_app_update_ui();
    std::swap(fleet[0],fleet[19]);fleet[0].icao24="new123";
    s.publish_result(fleet,PlanesService::MAX_PLANES,"Test");planes_app_update_ui();
    for (const auto &t : v.trails) assert(t.count==(std::string(t.icao)=="new123"?1:2));
    s._home_lat+=.1;s._center_lat=s._home_lat;planes_app_update_ui();
    for (const auto &t : v.trails) assert(t.count==1); // New origin resets all histories.
    s._home_lat=47.4;s._center_lat=47.4;
    // Airborne filtering occurs before the capped nearest-aircraft result.
    std::string payload="{\"ac\":[";
    for(int i=0;i<25;++i){if(i)payload+=",";payload+="{\"hex\":\"g"+std::to_string(i)+"\",\"lat\":47.4,\"lon\":8.5,\"alt_baro\":\"ground\"}";}
    payload+=",{\"hex\":\"air\",\"lat\":47.41,\"lon\":8.5,\"alt_baro\":10000}]}";
    PlaneState parsed[PlanesService::MAX_PLANES];int count=0;std::string error;
    assert(s.parse_payload(payload,parsed,count,error));assert(count==1 && parsed[0].icao24=="air");
    // An in-flight request defers range/filter changes until it completes.
    s._fetch_in_progress=true;s.reset_search_area();assert(s._airborne_only && s.range_km()==25);
    s._fetch_in_progress=false;s.apply_search_area();assert(!s._airborne_only && s.range_km()==50);
    auto *old_screen=lv_screen_active();lv_screen_load(lv_obj_create(nullptr));lv_obj_delete(old_screen);
    assert(!app_ctx.nearby.root && !app_ctx.nearby.dialog);
    puts("Nearby aircraft: stable selection, navigation, units, empty/stale states and airborne filtering passed");
}
