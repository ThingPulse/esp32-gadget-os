#include "ui/NearbyPlanesView.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <cctype>

namespace {
constexpr double PI = 3.14159265358979323846;
constexpr int RADAR_X = 150, RADAR_Y = 128, RADAR_RADIUS = 112;
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
        {"A20N","Airbus A320neo"},{"A21N","Airbus A321neo"},{"A321","Airbus A321"},{"A339","Airbus A330-900"},{"BCS3","Airbus A220-300"},
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
void visible(lv_obj_t *obj, bool show) {
    if (show) lv_obj_clear_flag(obj, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
}
void detail_text(lv_obj_t *obj, const std::string &text) {
    if (text != lv_label_get_text(obj)) lv_label_set_text(obj, text.c_str());
}
void update_details(NearbyPlanesView &v) {
    if (!v.dialog) return;
    const auto &service = PlanesService::instance();
    const PlaneState *plane = nullptr;
    for (int i = 0; i < service.plane_count(); ++i)
        if (service.planes()[i].icao24 == v.dialog_icao) plane = &service.planes()[i];
    visible(v.dialog_content, plane != nullptr);
    visible(v.dialog_body, !plane || !service.last_error().empty());
    detail_text(v.dialog_body, plane ? "Saved data - update failed" :
                "This aircraft is no longer in the nearby results.");
    if (!plane) return;
    detail_text(v.dialog_title, plane->callsign.empty() ? plane->icao24 : plane->callsign);
    detail_text(v.dialog_airline, airline(*plane));
    detail_text(v.dialog_type, type_name(*plane));
    const auto route = airport_route(*plane);
    visible(v.dialog_route, !route.empty());
    visible(v.dialog_route_missing, route.empty());
    if (!route.empty()) {
        const auto split = route.find(" " LV_SYMBOL_RIGHT " ");
        detail_text(v.dialog_codes[0], route.substr(0, split));
        detail_text(v.dialog_codes[1], route.substr(split + std::strlen(" " LV_SYMBOL_RIGHT " ")));
        detail_text(v.dialog_airports[0], plane->origin_airport_name);
        detail_text(v.dialog_airports[1], plane->destination_airport_name);
    }
    char value[64];
    std::snprintf(value, sizeof(value), "%.0f %s", service.is_metric() ? plane->altitude_m : plane->altitude_m * 3.28084,
                  service.is_metric() ? "m" : "ft");
    std::string altitude = value;
    for (int pos = int(altitude.find(' ')) - 3; pos > 0 && altitude[pos - 1] != '-'; pos -= 3)
        altitude.insert(pos, ",");
    detail_text(v.dialog_stats[0], altitude);
    std::snprintf(value, sizeof(value), "%.0f %s", service.is_metric() ? plane->velocity_ms * 3.6 : plane->velocity_ms * 1.943844,
                  service.is_metric() ? "km/h" : "kt");
    detail_text(v.dialog_stats[1], value);
    std::snprintf(value, sizeof(value), "%.1f %s", service.is_metric() ? plane->distance_km : plane->distance_km / 1.852,
                  service.is_metric() ? "km" : "nm");
    detail_text(v.dialog_stats[2], value);
    std::string bearing = direction(plane->bearing_deg);
    bearing[0] = std::toupper(static_cast<unsigned char>(bearing[0]));
    detail_text(v.dialog_stats[3], bearing);
    detail_text(v.dialog_registration, plane->registration.empty() ? "Unknown" : plane->registration);
    detail_text(v.dialog_hex, "ICAO  " + plane->icao24);
    detail_text(v.dialog_squawk, "Squawk  " + (plane->squawk.empty() ? std::string("--") : plane->squawk));
}
lv_obj_t *detail_column(lv_obj_t *parent, int gap = 6) {
    auto *obj = lv_obj_create(parent); plain(obj);
    lv_obj_set_size(obj, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(obj, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(obj, gap, 0);
    return obj;
}
void detail_divider(lv_obj_t *parent) {
    auto *obj = lv_obj_create(parent); plain(obj);
    lv_obj_set_size(obj, lv_pct(100), 1);
    lv_obj_set_style_bg_color(obj, lv_color_hex(0x484e69), 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
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
        view.dialog_content = view.dialog_airline = view.dialog_type = nullptr;
        view.dialog_route = view.dialog_route_missing = nullptr;
        view.dialog_codes = {}; view.dialog_airports = {}; view.dialog_stats = {};
        view.dialog_registration = view.dialog_hex = view.dialog_squawk = nullptr;
        view.dialog_icao.clear();
    }, LV_EVENT_DELETE, &v);
    auto *panel = lv_obj_create(overlay);
    lv_obj_set_size(panel, 300, std::min(408, int(lv_display_get_vertical_resolution(nullptr)) - 24));
    lv_obj_center(panel);
    lv_obj_set_style_pad_all(panel, 12, 0);
    lv_obj_set_style_pad_row(panel, 8, 0);
    lv_obj_set_style_border_width(panel, 0, 0);
    lv_obj_set_style_bg_color(panel, lv_color_hex(0x2a2d42), 0);
    lv_obj_set_flex_flow(panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE);
    auto *scroll = detail_column(panel);
    lv_obj_set_height(scroll, 0);
    lv_obj_set_flex_grow(scroll, 1);
    lv_obj_add_flag(scroll, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(scroll, LV_DIR_VER);
    lv_obj_clear_flag(scroll, LV_OBJ_FLAG_SCROLL_CHAIN);
    v.dialog_body = label(scroll, 0, 0, 268, "");
    lv_label_set_long_mode(v.dialog_body, LV_LABEL_LONG_WRAP);
    v.dialog_content = detail_column(scroll, 7);
    auto *header = detail_column(v.dialog_content, 2);
    v.dialog_airline = label(header, 0, 0, 276, "", MINT);
    v.dialog_title = label(header, 0, 0, 276, "");
    lv_obj_set_style_text_font(v.dialog_title, &lv_font_montserrat_24, 0);
    v.dialog_type = label(header, 0, 0, 276, "");
    lv_label_set_long_mode(v.dialog_type, LV_LABEL_LONG_WRAP);
    detail_divider(v.dialog_content);
    v.dialog_route_missing = label(v.dialog_content, 0, 0, 276, "Route unavailable", 0x9aa5bd);
    v.dialog_route = detail_column(v.dialog_content, 3);
    auto *codes = lv_obj_create(v.dialog_route); plain(codes);lv_obj_set_size(codes, 276, 30);
    v.dialog_codes[0] = label(codes, 0, 0, 106, "", MINT);
    v.dialog_codes[1] = label(codes, 170, 0, 106, "", MINT);
    for (auto *code : v.dialog_codes) lv_obj_set_style_text_font(code, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_align(v.dialog_codes[1], LV_TEXT_ALIGN_RIGHT, 0);
    // Small plane and direction arrow, drawn with the same lightweight strokes as the radar.
    lv_obj_add_event_cb(codes, [](lv_event_t *event) {
        lv_area_t area;lv_obj_get_coords(static_cast<lv_obj_t *>(lv_event_get_target(event)), &area);
        auto *layer = lv_event_get_layer(event);
        const int segments[][4] = {{112,15,128,15},{119,7,123,15},{119,23,123,15},
                                  {113,11,115,15},{113,19,115,15},{134,15,160,15},
                                  {155,10,160,15},{155,20,160,15}};
        for (const auto &segment : segments) {
            lv_draw_line_dsc_t line;lv_draw_line_dsc_init(&line);
            line.p1 = {lv_value_precise_t(area.x1 + segment[0]), lv_value_precise_t(area.y1 + segment[1])};
            line.p2 = {lv_value_precise_t(area.x1 + segment[2]), lv_value_precise_t(area.y1 + segment[3])};
            line.color = lv_color_hex(0x9aa5bd);line.width = 2;line.round_start = line.round_end = 1;
            lv_draw_line(layer, &line);
        }
    }, LV_EVENT_DRAW_MAIN, nullptr);
    auto *names = lv_obj_create(v.dialog_route); plain(names);
    lv_obj_set_size(names, 276, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(names, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(names, 16, 0);
    for (int i = 0; i < 2; ++i) {
        v.dialog_airports[i] = label(names, 0, 0, 130, "");
        // Reserve exactly two lines: long official names must not push the
        // flight stats or identifiers below the fixed Close button.
        lv_obj_set_height(v.dialog_airports[i],
            2 * lv_obj_get_style_text_font(v.dialog_airports[i], LV_PART_MAIN)->line_height +
            lv_obj_get_style_text_line_space(v.dialog_airports[i], LV_PART_MAIN));
        lv_label_set_long_mode(v.dialog_airports[i], LV_LABEL_LONG_DOT);
    }
    lv_obj_set_style_text_align(v.dialog_airports[1], LV_TEXT_ALIGN_RIGHT, 0);
    detail_divider(v.dialog_content);
    auto *stats = lv_obj_create(v.dialog_content); plain(stats);lv_obj_set_size(stats, 276, 116);
    const char *titles[] = {"Altitude", "Speed", "Distance", "Direction"};
    for (int i = 0; i < 4; ++i) {
        const int x = (i % 2) * 146, y = (i / 2) * 68;
        label(stats, x, y, 130, titles[i], 0x9aa5bd);
        v.dialog_stats[i] = label(stats, x, y + 18, 130, "");
        lv_obj_set_style_text_font(v.dialog_stats[i], &lv_font_montserrat_24, 0);
    }
    label(stats, 0, 45, 138, "above sea level", 0x9aa5bd);
    detail_divider(v.dialog_content);
    auto *ids = lv_obj_create(v.dialog_content); plain(ids);lv_obj_set_size(ids, 276, 42);
    label(ids, 0, 0, 112, "Registration", 0x9aa5bd);
    v.dialog_registration = label(ids, 116, 0, 160, "");
    v.dialog_hex = label(ids, 0, 24, 138, "", 0x9aa5bd);
    v.dialog_squawk = label(ids, 146, 24, 130, "", 0x9aa5bd);
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
    const double r = RADAR_RADIUS * std::min(1.0, double(p.distance_km) / v.radius_km);
    const double a = p.bearing_deg * PI / 180;
    return {int32_t(RADAR_X + r * std::sin(a)), int32_t(RADAR_Y - r * std::cos(a))};
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
    for (int r : {RADAR_RADIUS / 2, RADAR_RADIUS}) circle(layer, x+RADAR_X, y+RADAR_Y, r, 0x526480);
    line(layer,x+RADAR_X-RADAR_RADIUS,y+RADAR_Y,x+RADAR_X+RADAR_RADIUS,y+RADAR_Y,0x35445e);
    line(layer,x+RADAR_X,y+RADAR_Y-RADAR_RADIUS,x+RADAR_X,y+RADAR_Y+RADAR_RADIUS,0x35445e);
    circle(layer,x+RADAR_X,y+RADAR_Y,3,0xe7eaf4,3);
    const auto *trail = selected_trail(v);
    if (selected(v) && trail) for (int i = 1; i < trail->count; ++i) {
        auto from = trail->point(i - 1), to = trail->point(i);
        if (!clip_trail(from, to, v.radius_km)) continue;
        const double scale = RADAR_RADIUS / v.radius_km;
        const auto opacity = static_cast<lv_opa_t>(60 + 170 * i / (trail->count - 1));
        line(layer, x + RADAR_X + from.east_km * scale, y + RADAR_Y - from.north_km * scale,
             x + RADAR_X + to.east_km * scale, y + RADAR_Y - to.north_km * scale, MINT, 2, opacity);
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
    lv_dropdown_set_options_static(range,"5 km\n10 km\n25 km\n50 km");lv_dropdown_set_selected(range,2);
    lv_obj_add_event_cb(range,[](lv_event_t *e){
        auto &v=*static_cast<NearbyPlanesView *>(lv_event_get_user_data(e));
        static const double radii[]={5,10,25,50};
        v.radius_km=radii[lv_dropdown_get_selected(v.range_label)];
        if(v.range_changed)v.range_changed();
    },LV_EVENT_VALUE_CHANGED,&v);
    v.radar=lv_obj_create(v.root);plain(v.radar);lv_obj_set_pos(v.radar,10,30);lv_obj_set_size(v.radar,300,262);
    lv_obj_add_event_cb(v.radar,draw_radar,LV_EVENT_DRAW_MAIN,&v);
    label(v.radar,144,0,20,"N");label(v.radar,144,242,20,"S");label(v.radar,18,119,20,"W");label(v.radar,267,119,20,"E");
    label(v.radar,137,137,30,"You");label(v.radar,231,238,68,"North up",0x9aa5bd);
    lv_obj_add_event_cb(v.radar,[](lv_event_t *e){
        auto &v=*static_cast<NearbyPlanesView *>(lv_event_get_user_data(e));
        auto *input=lv_indev_active();if(!input)return;
        lv_point_t touch;lv_indev_get_point(input,&touch);lv_area_t a;lv_obj_get_coords(v.radar,&a);
        int best=-1;double distance=25*25;
        for(int n=0;n<v.count;++n){const auto &p=PlanesService::instance().planes()[v.indices[n]];const auto pos=position(v,p);
            double dx=touch.x-a.x1-pos.x,dy=touch.y-a.y1-pos.y;if(dx*dx+dy*dy<distance){best=v.indices[n];distance=dx*dx+dy*dy;}}
        if(best>=0){v.selected_icao=PlanesService::instance().planes()[best].icao24;nearby_planes_update(v);}
    },LV_EVENT_CLICKED,&v);
    v.card=lv_obj_create(v.root);plain(v.card);lv_obj_set_pos(v.card,10,294);lv_obj_set_size(v.card,300,108);
    lv_obj_set_style_border_width(v.card,1,0);lv_obj_set_style_border_color(v.card,lv_color_hex(MINT),0);
    lv_obj_set_style_bg_color(v.card,lv_color_hex(0x293448),LV_STATE_PRESSED);lv_obj_set_style_bg_opa(v.card,LV_OPA_COVER,LV_STATE_PRESSED);
    v.look=label(v.card,10,7,280,"Looking for aircraft",MINT);lv_obj_set_style_text_font(v.look,&lv_font_montserrat_24,0);
    label(v.card,280,13,14,LV_SYMBOL_RIGHT,MINT);
    v.route=label(v.card,10,70,280,"",MINT);
    v.distance=label(v.card,10,38,280,"");v.identity=label(v.card,10,61,280,"");v.type=label(v.card,10,83,280,"");
    lv_obj_add_event_cb(v.card,[](lv_event_t *e){auto &v=*static_cast<NearbyPlanesView *>(lv_event_get_user_data(e));show_details(v);},LV_EVENT_CLICKED,&v);
    v.status=label(v.root,10,412,152,"Waiting for aircraft",0x9aa5bd);
    v.page=label(v.root,216,412,46,"0/0",0x9aa5bd);
    lv_obj_set_style_text_align(v.page,LV_TEXT_ALIGN_CENTER,0);
    v.previous=lv_button_create(v.root);
    v.next=lv_button_create(v.root);
    for (auto *button : {v.previous,v.next}) {
        plain(button);
        lv_obj_set_pos(button,button==v.previous?170:266,404);
        lv_obj_set_size(button,44,32);
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
