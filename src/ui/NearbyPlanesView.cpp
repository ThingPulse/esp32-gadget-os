#include "ui/NearbyPlanesView.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <cctype>

namespace {
constexpr double PI = 3.14159265358979323846;
constexpr uint32_t MINT = 0x63e6be;
void plain(lv_obj_t *o) {
    lv_obj_set_style_pad_all(o, 0, 0);
    lv_obj_set_style_border_width(o, 0, 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_TRANSP, 0);
    lv_obj_set_style_shadow_width(o, 0, 0);
    lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLLABLE);
}
lv_obj_t *label(lv_obj_t *p, int x, int y, int w, const char *text, uint32_t color = 0xe7eaf4) {
    auto *o = lv_label_create(p);
    lv_obj_set_pos(o, x, y); lv_obj_set_width(o, w);
    lv_label_set_long_mode(o, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_color(o, lv_color_hex(color), 0);
    lv_label_set_text(o, text);
    return o;
}
const char *airline(const PlaneState &p) {
    struct Entry { const char *prefix, *name; };
    static const Entry names[] = {{"SWR","SWISS"},{"CFG","Condor"},{"DLH","Lufthansa"},
        {"BAW","British Airways"},{"KLM","KLM"},{"AFR","Air France"},{"EZY","easyJet"},
        {"RYR","Ryanair"},{"UAE","Emirates"},{"QTR","Qatar Airways"},{"DAL","Delta"},
        {"UAL","United"},{"AAL","American"},{"EIN","Aer Lingus"}};
    for (const auto &n : names) if (p.callsign.compare(0, 3, n.prefix) == 0) return n.name;
    return "Aircraft";
}
const char *type_name(const PlaneState &p) {
    if (!p.aircraft_description.empty()) return p.aircraft_description.c_str();
    struct Entry { const char *code, *name; };
    static const Entry names[] = {{"E190","Embraer 190"},{"E195","Embraer 195"},
        {"E290","Embraer E190-E2"},{"E295","Embraer E195-E2"},{"A320","Airbus A320"},
        {"A321","Airbus A321"},{"A339","Airbus A330-900"},{"BCS3","Airbus A220-300"},
        {"B738","Boeing 737-800"},{"B77W","Boeing 777-300ER"}};
    for (const auto &n : names) if (p.aircraft_type == n.code) return n.name;
    return p.aircraft_type.empty() ? "Unknown type" : p.aircraft_type.c_str();
}
const char *direction(float b, bool short_name = false) {
    static const char *names[] = {"north","northeast","east","southeast","south","southwest","west","northwest"};
    static const char *shorts[] = {"N","NE","E","SE","S","SW","W","NW"};
    const int i = int(std::lround(b / 45)) & 7;
    return short_name ? shorts[i] : names[i];
}
const char *motion(const PlaneState &p) {
    if (!p.heading_valid || !std::isfinite(p.heading_deg) || p.velocity_ms < 5) return "";
    const double outward = std::cos((p.heading_deg - p.bearing_deg) * PI / 180);
    return outward < -.35 ? "Approaching" : outward > .35 ? "Moving away" : "Passing by";
}
const PlaneState *selected(const NearbyPlanesView &v) {
    const auto &s = PlanesService::instance();
    for (int n = 0; n < v.count; ++n) {
        const auto &p = s.planes()[v.indices[n]];
        if (p.icao24 == v.selected_icao) return &p;
    }
    return nullptr;
}
std::string airport_route(const PlaneState &plane) {
    const size_t arrow = plane.route.find("->");
    if (arrow == std::string::npos) return "";
    auto code = [](std::string text) {
        const auto first = text.find_first_not_of(" \t");
        if (first == std::string::npos) return std::string{};
        text = text.substr(first, text.find_last_not_of(" \t") - first + 1);
        if (text.size() < 3 || text.size() > 4) return std::string{};
        for (char ch : text) if (!std::isalnum(static_cast<unsigned char>(ch))) return std::string{};
        if (text == "null" || text == "NULL") return std::string{};
        for (char &ch : text) ch = std::toupper(static_cast<unsigned char>(ch));
        return text;
    };
    const auto origin = code(plane.route.substr(0, arrow));
    const auto destination = code(plane.route.substr(arrow + 2));
    if (origin.empty() || destination.empty()) return "";
    return origin + " " LV_SYMBOL_RIGHT " " + destination;
}
void update_details(NearbyPlanesView &v) {
    if (!v.dialog) return;
    const auto &service = PlanesService::instance();
    const PlaneState *plane = nullptr;
    for (int i = 0; i < service.plane_count(); ++i)
        if (service.planes()[i].icao24 == v.dialog_icao) plane = &service.planes()[i];
    if (!plane) {
        lv_label_set_text(v.dialog_body, "This aircraft is no longer in the nearby results.");
        return;
    }
    lv_label_set_text(v.dialog_title, plane->callsign.empty() ? plane->icao24.c_str() : plane->callsign.c_str());
    std::string text = std::string(airline(*plane)) + "\n" + type_name(*plane);
    const auto route = airport_route(*plane);
    if (!route.empty()) {
        text += "\n\n" + route;
        if (!plane->origin_airport_name.empty()) text += "\nFrom: " + plane->origin_airport_name;
        if (!plane->destination_airport_name.empty()) text += "\nTo: " + plane->destination_airport_name;
    } else text += "\n\nRoute unavailable";
    char values[160];
    std::snprintf(values, sizeof(values), "\n\nAltitude: %.0f %s (sea level)\nSpeed: %.0f %s\nDistance: %.1f %s %s",
        service.is_metric() ? plane->altitude_m : plane->altitude_m * 3.28084,
        service.is_metric() ? "m" : "ft", service.is_metric() ? plane->velocity_ms * 3.6 : plane->velocity_ms * 1.943844,
        service.is_metric() ? "km/h" : "kt", service.is_metric() ? plane->distance_km : plane->distance_km / 1.852,
        service.is_metric() ? "km" : "nm", direction(plane->bearing_deg, true));
    text += values;
    if (!plane->registration.empty()) text += "\nRegistration: " + plane->registration;
    text += "\nICAO: " + plane->icao24;
    if (!plane->squawk.empty()) text += "\nSquawk: " + plane->squawk;
    if (!service.last_error().empty()) text += "\n\nSaved data - update failed";
    if (std::strcmp(lv_label_get_text(v.dialog_body), text.c_str()))
        lv_label_set_text(v.dialog_body, text.c_str());
}
void show_details(NearbyPlanesView &v) {
    const auto *plane = selected(v);
    if (!plane || v.dialog) return;
    v.dialog_icao = plane->icao24;
    auto *overlay = lv_obj_create(lv_obj_get_screen(v.root));
    v.dialog = overlay;
    plain(overlay);
    lv_obj_set_size(overlay, lv_pct(100), lv_pct(100));
    lv_obj_set_style_radius(overlay, 0, 0);
    lv_obj_set_style_bg_color(overlay, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(overlay, LV_OPA_60, 0);
    lv_obj_add_event_cb(overlay, [](lv_event_t *event) {
        auto &view = *static_cast<NearbyPlanesView *>(lv_event_get_user_data(event));
        view.dialog = view.dialog_title = view.dialog_body = view.dialog_close = nullptr;
        view.dialog_icao.clear();
    }, LV_EVENT_DELETE, &v);
    auto *panel = lv_obj_create(overlay);
    lv_obj_set_size(panel, 296, std::min(370, int(lv_display_get_vertical_resolution(nullptr)) - 24));
    lv_obj_center(panel);
    lv_obj_set_style_pad_all(panel, 14, 0);
    lv_obj_set_style_pad_row(panel, 10, 0);
    lv_obj_set_style_border_width(panel, 0, 0);
    lv_obj_set_style_bg_color(panel, lv_color_hex(0x2a2d42), 0);
    lv_obj_set_flex_flow(panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE);
    v.dialog_title = label(panel, 0, 0, 268, "Flight details", MINT);
    lv_obj_set_style_text_font(v.dialog_title, &lv_font_montserrat_24, 0);
    auto *scroll = lv_obj_create(panel);
    plain(scroll);
    lv_obj_set_width(scroll, lv_pct(100));
    lv_obj_set_flex_grow(scroll, 1);
    lv_obj_add_flag(scroll, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(scroll, LV_DIR_VER);
    lv_obj_clear_flag(scroll, LV_OBJ_FLAG_SCROLL_CHAIN);
    v.dialog_body = label(scroll, 0, 0, 256, "");
    lv_label_set_long_mode(v.dialog_body, LV_LABEL_LONG_WRAP);
    v.dialog_close = lv_button_create(panel);
    lv_obj_set_size(v.dialog_close, lv_pct(100), 40);
    auto *close_label = lv_label_create(v.dialog_close);
    lv_label_set_text(close_label, "Close");
    lv_obj_center(close_label);
    lv_obj_add_event_cb(v.dialog_close, [](lv_event_t *event) {
        lv_obj_delete_async(static_cast<lv_obj_t *>(lv_event_get_user_data(event)));
    }, LV_EVENT_CLICKED, overlay);
    update_details(v);
}
void step_selection(NearbyPlanesView &v, int direction) {
    if (v.count < 2) return;
    int current = 0;
    for (int i = 0; i < v.count; ++i)
        if (PlanesService::instance().planes()[v.indices[i]].icao24 == v.selected_icao) current = i;
    const int next = (current + direction + v.count) % v.count;
    v.selected_icao = PlanesService::instance().planes()[v.indices[next]].icao24;
    nearby_planes_update(v);
}
lv_point_t position(const NearbyPlanesView &v, const PlaneState &p) {
    const double r = 76 * std::min(1.0, double(p.distance_km) / v.radius_km);
    const double a = p.bearing_deg * PI / 180;
    return {int32_t(150 + r * std::sin(a)), int32_t(91 - r * std::cos(a))};
}
const NearbyPlanesView::AircraftTrail *selected_trail(const NearbyPlanesView &v) {
    for (const auto &trail : v.trails)
        if (v.selected_icao == trail.icao) return &trail;
    return nullptr;
}
void record_trails(NearbyPlanesView &v) {
    const auto &service = PlanesService::instance();
    if (v.trail_latitude != service.home_latitude() ||
        v.trail_longitude != service.home_longitude()) {
        for (auto &trail : v.trails) trail = {};
        v.trail_latitude = service.home_latitude();
        v.trail_longitude = service.home_longitude();
    }
    if (!service.has_data() ||
        std::abs(service.center_latitude() - v.trail_latitude) >= .00001 ||
        std::abs(service.center_longitude() - v.trail_longitude) >= .00001) return;
    auto eligible = [](const PlaneState &p) {
        return p.valid && !p.on_ground && !p.icao24.empty() && p.icao24.size() <= 6 &&
            std::isfinite(p.distance_km) && p.distance_km >= 0 && std::isfinite(p.bearing_deg);
    };
    // Protect every current aircraft before assigning slots to newcomers, so
    // provider ordering cannot evict another current aircraft's history.
    std::array<bool, PlanesService::MAX_PLANES> present{};
    for (int i = 0; i < service.plane_count(); ++i) {
        const auto &p = service.planes()[i];
        if (!eligible(p)) continue;
        for (size_t j = 0; j < v.trails.size(); ++j)
            if (p.icao24 == v.trails[j].icao) present[j] = true;
    }
    for (int i = 0; i < service.plane_count(); ++i) {
        const auto &p = service.planes()[i];
        if (!eligible(p)) continue;
        int slot = -1;
        for (size_t j = 0; j < v.trails.size(); ++j)
            if (p.icao24 == v.trails[j].icao) { slot = j; break; }
        if (slot < 0) {
            // Prefer unused slots, then reclaim a departed aircraft's slot.
            for (size_t j = 0; j < v.trails.size(); ++j)
                if (!v.trails[j].icao[0]) { slot = j; break; }
            if (slot < 0) for (size_t j = 0; j < v.trails.size(); ++j)
                if (!present[j]) { slot = j; break; }
            if (slot < 0) continue;
            v.trails[slot] = {};
            std::memcpy(v.trails[slot].icao, p.icao24.c_str(), p.icao24.size());
            present[slot] = true;
        }
        auto &trail = v.trails[slot];
        const double angle = p.bearing_deg * PI / 180;
        const NearbyPlanesView::TrailPoint point = {
            float(p.distance_km * std::sin(angle)), float(p.distance_km * std::cos(angle))};
        if (trail.count) {
            const auto &last = trail.point(trail.count - 1);
            if (std::abs(last.east_km - point.east_km) < 1e-7f &&
                std::abs(last.north_km - point.north_km) < 1e-7f) continue;
        }
        trail.points[trail.next] = point;
        trail.next = (trail.next + 1) % trail.points.size();
        if (trail.count < trail.points.size()) ++trail.count;
    }
}
// Clip a historical segment to the current radar radius, rather than pinning
// out-of-range positions to the rim when the user zooms in.
bool clip_trail(NearbyPlanesView::TrailPoint &a, NearbyPlanesView::TrailPoint &b, double radius) {
    const double dx = b.east_km - a.east_km, dy = b.north_km - a.north_km;
    const double aa = dx * dx + dy * dy;
    if (aa < 1e-14) return false;
    const double bb = 2 * (a.east_km * dx + a.north_km * dy);
    const double cc = a.east_km * a.east_km + a.north_km * a.north_km - radius * radius;
    const double discriminant = bb * bb - 4 * aa * cc;
    if (discriminant < 0) return false;
    const double start = std::max(0.0, (-bb - std::sqrt(discriminant)) / (2 * aa));
    const double end = std::min(1.0, (-bb + std::sqrt(discriminant)) / (2 * aa));
    if (start >= end) return false;
    b = {float(a.east_km + end * dx), float(a.north_km + end * dy)};
    a = {float(a.east_km + start * dx), float(a.north_km + start * dy)};
    return true;
}
void line(lv_layer_t *layer, double x, double y, double x2, double y2, uint32_t color, int width = 1, lv_opa_t opacity = LV_OPA_COVER) {
    lv_draw_line_dsc_t d; lv_draw_line_dsc_init(&d);
    d.p1 = {lv_value_precise_t(x), lv_value_precise_t(y)};
    d.p2 = {lv_value_precise_t(x2), lv_value_precise_t(y2)};
    d.color = lv_color_hex(color); d.width = width; d.opa = opacity;
    d.round_start = d.round_end = 1; lv_draw_line(layer, &d);
}
void circle(lv_layer_t *layer, int x, int y, int r, uint32_t color, int width = 1) {
    lv_draw_arc_dsc_t d; lv_draw_arc_dsc_init(&d);
    d.center = {x,y}; d.radius = r; d.start_angle = 0; d.end_angle = 360;
    d.width = width; d.color = lv_color_hex(color); d.opa = LV_OPA_COVER;
    lv_draw_arc(layer, &d);
}
void draw_radar(lv_event_t *e) {
    auto &v = *static_cast<NearbyPlanesView *>(lv_event_get_user_data(e));
    auto *layer = lv_event_get_layer(e);
    lv_area_t a; lv_obj_get_coords(v.radar, &a);
    const int x = a.x1, y = a.y1;
    for (int r : {38, 76}) circle(layer, x+150, y+91, r, 0x526480);
    line(layer,x+74,y+91,x+226,y+91,0x35445e);
    line(layer,x+150,y+15,x+150,y+167,0x35445e);
    circle(layer,x+150,y+91,3,0xe7eaf4,3);
    const auto *trail = selected_trail(v);
    if (selected(v) && trail) for (int i = 1; i < trail->count; ++i) {
        auto from = trail->point(i - 1), to = trail->point(i);
        if (!clip_trail(from, to, v.radius_km)) continue;
        const double scale = 76 / v.radius_km;
        const auto opacity = static_cast<lv_opa_t>(60 + 170 * i / (trail->count - 1));
        line(layer, x + 150 + from.east_km * scale, y + 91 - from.north_km * scale,
             x + 150 + to.east_km * scale, y + 91 - to.north_km * scale, MINT, 2, opacity);
    }
    for (int n = 0; n < v.count; ++n) {
        const auto &p = PlanesService::instance().planes()[v.indices[n]];
        const auto pos = position(v,p);
        const bool active = p.icao24 == v.selected_icao;
        const uint32_t color = active ? MINT : 0x96b4db;
        const int cx=x+pos.x, cy=y+pos.y;
        if (active) circle(layer,cx,cy,12,color);
        if (!p.heading_valid || !std::isfinite(p.heading_deg)) { circle(layer,cx,cy,4,color,3); continue; }
        const double angle = p.heading_deg * PI / 180;
        auto stroke = [&](double ax, double ay, double bx, double by) {
            auto px = [&](double xx, double yy) { return cx + xx*std::cos(angle)-yy*std::sin(angle); };
            auto py = [&](double xx, double yy) { return cy + xx*std::sin(angle)+yy*std::cos(angle); };
            line(layer,px(ax,ay),py(ax,ay),px(bx,by),py(bx,by),color,2);
        };
        stroke(0,-7,0,7); stroke(-7,2,0,-2); stroke(0,-2,7,2); stroke(-3,7,3,7);
    }
}
}
void nearby_planes_tick(NearbyPlanesView &v) {
    if (!v.status) return;
    const auto &s=PlanesService::instance();
    char text[64];
    if (s.is_fetching()) std::snprintf(text,sizeof(text),"Updating...");
    else if (!s.last_error().empty()) std::snprintf(text,sizeof(text),s.has_data()?"Saved data - retrying":"Update failed - retry");
    else if (!s.has_data()) std::snprintf(text,sizeof(text),"Waiting for aircraft");
    else std::snprintf(text,sizeof(text),"Updated %lus ago",s.data_age_ms()/1000);
    if (std::strcmp(lv_label_get_text(v.status),text)) lv_label_set_text(v.status,text);
}
void nearby_planes_update(NearbyPlanesView &v) {
    if (!v.root) return;
    const auto &s=PlanesService::instance();
    v.count=0;
    const bool home = std::abs(s.center_latitude()-s.home_latitude())<.00001 &&
                      std::abs(s.center_longitude()-s.home_longitude())<.00001;
    if (s.has_data() && home) for(int i=0;i<s.plane_count();++i) {
        const auto &p=s.planes()[i];
        if(p.valid && !p.on_ground && std::isfinite(p.distance_km) && std::isfinite(p.bearing_deg) &&
           p.distance_km>=0 && p.distance_km<=v.radius_km) v.indices[v.count++]=i;
    }
    std::sort(v.indices.begin(),v.indices.begin()+v.count,[&](int a,int b){return s.planes()[a].distance_km<s.planes()[b].distance_km;});
    if (!selected(v) && v.count) v.selected_icao=s.planes()[v.indices[0]].icao24;
    char text[160];
    std::snprintf(text,sizeof(text),"%d airborne nearby",v.count); lv_label_set_text(v.summary,text);
    lv_dropdown_set_options_static(v.range_label,s.is_metric()?"5 km\n10 km\n25 km\n50 km":"2.7 nm\n5.4 nm\n13.5 nm\n27 nm");
    lv_dropdown_set_selected(v.range_label,v.radius_km<=5?0:v.radius_km<=10?1:v.radius_km<=25?2:3);
    const auto *p=selected(v);
    PlanesService::instance().prioritize_aircraft(p ? p->icao24 : "");
    record_trails(v);
    const std::string route = p ? airport_route(*p) : "";
    lv_label_set_text(v.route, route.c_str());
    if (route.empty()) lv_obj_add_flag(v.route, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_clear_flag(v.route, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_y(v.distance, route.empty() ? 38 : 35);
    lv_obj_set_y(v.identity, route.empty() ? 61 : 52);
    lv_obj_set_y(v.type, route.empty() ? 83 : 88);
    int page = 0;
    for (int i = 0; i < v.count; ++i)
        if (s.planes()[v.indices[i]].icao24 == v.selected_icao) page = i + 1;
    std::snprintf(text, sizeof(text), "%d/%d", page, v.count);
    lv_label_set_text(v.page, text);
    for (auto *button : {v.previous, v.next}) {
        if (v.count < 2) lv_obj_add_state(button, LV_STATE_DISABLED);
        else lv_obj_remove_state(button, LV_STATE_DISABLED);
    }
    update_details(v);
    if (p) {
        std::snprintf(text,sizeof(text),"Look %s",direction(p->bearing_deg)); lv_label_set_text(v.look,text);
        const char *movement=motion(*p);
        std::snprintf(text,sizeof(text),"%.1f %s away%s%s",s.is_metric()?p->distance_km:p->distance_km/1.852,
            s.is_metric()?"km":"nm",*movement?" - ":"",movement); lv_label_set_text(v.distance,text);
        std::snprintf(text,sizeof(text),"%s - %s",airline(*p),p->callsign.empty()?p->icao24.c_str():p->callsign.c_str()); lv_label_set_text(v.identity,text);
        std::snprintf(text,sizeof(text),"%s | %.0f %s alt.",type_name(*p),s.is_metric()?p->altitude_m:p->altitude_m*3.28084,s.is_metric()?"m":"ft"); lv_label_set_text(v.type,text);
        lv_obj_remove_state(v.card,LV_STATE_DISABLED);
    } else {
        lv_label_set_text(v.look,s.is_configured()?(s.has_data()&&home?"No aircraft nearby":"Looking for aircraft..."):"Set your location");
        lv_label_set_text(v.distance,s.is_configured()?"Try a larger radius":"Settings > Planes");
        lv_label_set_text(v.identity,"Airborne aircraft appear here"); lv_label_set_text(v.type,"");
        lv_obj_add_state(v.card,LV_STATE_DISABLED);
    }
    v.alternatives={{-1,-1}};
    for(int n=0,k=0;n<v.count&&k<2;++n) if(s.planes()[v.indices[n]].icao24!=v.selected_icao) v.alternatives[k++]=v.indices[n];
    for(int k=0;k<2;++k) {
        if(v.alternatives[k]<0) {lv_obj_add_flag(v.rows[k],LV_OBJ_FLAG_HIDDEN);continue;}
        const auto &other=s.planes()[v.alternatives[k]];
        lv_obj_clear_flag(v.rows[k],LV_OBJ_FLAG_HIDDEN);
        std::snprintf(text,sizeof(text),"%s   %.1f %s %s " LV_SYMBOL_RIGHT,airline(other),s.is_metric()?other.distance_km:other.distance_km/1.852,
                      s.is_metric()?"km":"nm",direction(other.bearing_deg,true));lv_label_set_text(v.row_labels[k],text);
    }
    lv_obj_invalidate(v.radar); nearby_planes_tick(v);
}
void nearby_planes_create(NearbyPlanesView &v,lv_obj_t *parent) {
    v.root=lv_obj_create(parent);plain(v.root);lv_obj_set_size(v.root,320,436);lv_obj_align(v.root,LV_ALIGN_TOP_MID,0,0);
    v.summary=label(v.root,10,10,210,"Looking for aircraft");
    auto *range=lv_dropdown_create(v.root);lv_obj_set_pos(range,224,0);lv_obj_set_size(range,86,38);
    lv_obj_set_style_bg_color(range,lv_color_hex(0x293448),0);
    lv_obj_set_style_text_color(range,lv_color_hex(0x96b4db),0);
    lv_obj_set_style_border_color(range,lv_color_hex(0x526480),0);
    v.range_label=range;
    lv_dropdown_set_options_static(range,"5 km\n10 km\n25 km\n50 km");lv_dropdown_set_selected(range,1);
    lv_obj_add_event_cb(range,[](lv_event_t *e){
        auto &v=*static_cast<NearbyPlanesView *>(lv_event_get_user_data(e));
        static const double radii[]={5,10,25,50};
        v.radius_km=radii[lv_dropdown_get_selected(v.range_label)];
        if(v.range_changed)v.range_changed();
    },LV_EVENT_VALUE_CHANGED,&v);
    v.radar=lv_obj_create(v.root);plain(v.radar);lv_obj_set_pos(v.radar,10,30);lv_obj_set_size(v.radar,300,184);
    lv_obj_add_event_cb(v.radar,draw_radar,LV_EVENT_DRAW_MAIN,&v);
    label(v.radar,144,0,20,"N");label(v.radar,144,167,20,"S");label(v.radar,56,82,20,"W");label(v.radar,230,82,20,"E");
    label(v.radar,137,100,30,"You");label(v.radar,231,158,68,"North up",0x9aa5bd);
    lv_obj_add_event_cb(v.radar,[](lv_event_t *e){
        auto &v=*static_cast<NearbyPlanesView *>(lv_event_get_user_data(e));
        auto *input=lv_indev_active();if(!input)return;
        lv_point_t touch;lv_indev_get_point(input,&touch);lv_area_t a;lv_obj_get_coords(v.radar,&a);
        int best=-1;double distance=25*25;
        for(int n=0;n<v.count;++n){const auto &p=PlanesService::instance().planes()[v.indices[n]];const auto pos=position(v,p);
            double dx=touch.x-a.x1-pos.x,dy=touch.y-a.y1-pos.y;if(dx*dx+dy*dy<distance){best=v.indices[n];distance=dx*dx+dy*dy;}}
        if(best>=0){v.selected_icao=PlanesService::instance().planes()[best].icao24;nearby_planes_update(v);}
    },LV_EVENT_CLICKED,&v);
    v.card=lv_obj_create(v.root);plain(v.card);lv_obj_set_pos(v.card,10,216);lv_obj_set_size(v.card,300,108);
    lv_obj_set_style_border_width(v.card,1,0);lv_obj_set_style_border_color(v.card,lv_color_hex(MINT),0);
    lv_obj_set_style_bg_color(v.card,lv_color_hex(0x293448),LV_STATE_PRESSED);lv_obj_set_style_bg_opa(v.card,LV_OPA_COVER,LV_STATE_PRESSED);
    v.look=label(v.card,10,7,280,"Looking for aircraft",MINT);lv_obj_set_style_text_font(v.look,&lv_font_montserrat_24,0);
    label(v.card,280,13,14,LV_SYMBOL_RIGHT,MINT);
    v.route=label(v.card,10,70,280,"",MINT);
    v.distance=label(v.card,10,38,280,"");v.identity=label(v.card,10,61,280,"");v.type=label(v.card,10,83,280,"");
    lv_obj_add_event_cb(v.card,[](lv_event_t *e){auto &v=*static_cast<NearbyPlanesView *>(lv_event_get_user_data(e));show_details(v);},LV_EVENT_CLICKED,&v);
    for(int k=0;k<2;++k){
        v.rows[k]=lv_obj_create(v.root);plain(v.rows[k]);lv_obj_set_pos(v.rows[k],10,328+k*34);lv_obj_set_size(v.rows[k],300,32);
        lv_obj_set_style_bg_color(v.rows[k],lv_color_hex(0x293448),0);lv_obj_set_style_bg_opa(v.rows[k],LV_OPA_COVER,0);
        v.row_labels[k]=label(v.rows[k],10,8,280,"");
        lv_obj_add_event_cb(v.rows[k],[](lv_event_t *e){auto &v=*static_cast<NearbyPlanesView *>(lv_event_get_user_data(e));int k=lv_event_get_target(e)==v.rows[0]?0:1;
            if(v.alternatives[k]>=0){v.selected_icao=PlanesService::instance().planes()[v.alternatives[k]].icao24;nearby_planes_update(v);}},LV_EVENT_CLICKED,&v);
    }
    v.status=label(v.root,10,412,152,"Waiting for aircraft",0x9aa5bd);
    v.page=label(v.root,216,412,46,"0/0",0x9aa5bd);
    lv_obj_set_style_text_align(v.page,LV_TEXT_ALIGN_CENTER,0);
    v.previous=lv_button_create(v.root);
    v.next=lv_button_create(v.root);
    for (auto *button : {v.previous,v.next}) {
        plain(button);
        lv_obj_set_pos(button,button==v.previous?170:266,396);
        lv_obj_set_size(button,44,40);
        auto *caption=lv_label_create(button);
        lv_label_set_text(caption,button==v.previous?LV_SYMBOL_LEFT:LV_SYMBOL_RIGHT);
        lv_obj_set_style_text_color(caption,lv_color_hex(0x96b4db),0);
        lv_obj_center(caption);
        lv_obj_add_event_cb(button,[](lv_event_t *event) {
            auto &view=*static_cast<NearbyPlanesView *>(lv_event_get_user_data(event));
            step_selection(view,lv_event_get_target(event)==view.previous?-1:1);
        },LV_EVENT_CLICKED,&v);
    }
}
