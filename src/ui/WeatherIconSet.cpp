#include "ui/WeatherIconSet.h"

namespace {

void style_base(lv_obj_t *obj)
{
    if (!obj) return;
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_border_width(obj, 0, 0);
    lv_obj_set_style_radius(obj, 0, 0);
    lv_obj_set_style_pad_all(obj, 0, 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_TRANSP, 0);
}

lv_obj_t *make_rect(lv_obj_t *parent, lv_coord_t x, lv_coord_t y, lv_coord_t w, lv_coord_t h, lv_color_t color, lv_opa_t opa = LV_OPA_COVER)
{
    lv_obj_t *obj = lv_obj_create(parent);
    if (!obj) return nullptr;
    style_base(obj);
    lv_obj_set_pos(obj, x, y);
    lv_obj_set_size(obj, w, h);
    lv_obj_set_style_bg_color(obj, color, 0);
    lv_obj_set_style_bg_opa(obj, opa, 0);
    return obj;
}

lv_obj_t *make_circle(lv_obj_t *parent, lv_coord_t x, lv_coord_t y, lv_coord_t d, lv_color_t color, lv_opa_t opa = LV_OPA_COVER)
{
    lv_obj_t *obj = make_rect(parent, x, y, d, d, color, opa);
    if (!obj) return nullptr;
    lv_obj_set_style_radius(obj, LV_RADIUS_CIRCLE, 0);
    return obj;
}

static void line_points_deleted(lv_event_t *e)
{
    void *points = lv_event_get_user_data(e);
    if(points) lv_free(points);
}

lv_obj_t *make_line(lv_obj_t *parent,
                    lv_coord_t size,
                    lv_coord_t x1,
                    lv_coord_t y1,
                    lv_coord_t x2,
                    lv_coord_t y2,
                    lv_coord_t width,
                    lv_color_t color)
{
    lv_point_precise_t *points = static_cast<lv_point_precise_t *>(lv_malloc(sizeof(lv_point_precise_t) * 2));
    if(!points) return nullptr;

    points[0] = {x1, y1};
    points[1] = {x2, y2};

    lv_obj_t *line = lv_line_create(parent);
    if(!line) {
        lv_free(points);
        return nullptr;
    }
    style_base(line);
    lv_obj_set_pos(line, 0, 0);
    lv_obj_set_size(line, size, size);
    lv_line_set_points(line, points, 2);
    lv_obj_set_style_line_width(line, width, 0);
    lv_obj_set_style_line_color(line, color, 0);
    lv_obj_set_style_line_rounded(line, true, 0);
    lv_obj_add_event_cb(line, line_points_deleted, LV_EVENT_DELETE, points);
    return line;
}

void draw_cloud(lv_obj_t *root, lv_coord_t size, lv_color_t color)
{
    lv_coord_t y = size * 5 / 16;
    const lv_coord_t side_d = (size * 5 + 8) / 16;
    const lv_coord_t mid_d = (size * 6 + 8) / 16;
    const lv_coord_t left_x = (size * 2 + 8) / 16;
    const lv_coord_t mid_x = (size * 5 + 8) / 16;
    const lv_coord_t right_x = (size * 9 + 8) / 16;
    const lv_coord_t side_y = y + (size * 2 + 8) / 16;

    make_circle(root, left_x, side_y, side_d, color);
    make_circle(root, mid_x, y, mid_d, color);
    make_circle(root, right_x, side_y, side_d, color);

    // Bridge the side circles using their centers so the body stays flat without dropping too low.
    const lv_coord_t left_center_x = left_x + side_d / 2;
    const lv_coord_t right_center_x = right_x + side_d / 2;
    const lv_coord_t side_center_y = side_y + side_d / 2;
    make_rect(root,
              left_center_x,
              side_center_y - side_d / 2,
              right_center_x - left_center_x,
              side_d,
              color);
}

void draw_sun(lv_obj_t *root, lv_coord_t size)
{
    lv_color_t sun = lv_color_hex(0xf4c542);
    lv_coord_t c = size / 2;
    lv_coord_t core = size * 6 / 16;
    lv_coord_t ray = size * 4 / 16;
    lv_coord_t thickness = LV_MAX(2, size / 18);
    lv_coord_t diag = (ray * 707 + 500) / 1000;

    make_line(root, size, c, c, c, c - ray, thickness, sun);
    make_line(root, size, c, c, c + diag, c - diag, thickness, sun);
    make_line(root, size, c, c, c + ray, c, thickness, sun);
    make_line(root, size, c, c, c + diag, c + diag, thickness, sun);
    make_line(root, size, c, c, c, c + ray, thickness, sun);
    make_line(root, size, c, c, c - diag, c + diag, thickness, sun);
    make_line(root, size, c, c, c - ray, c, thickness, sun);
    make_line(root, size, c, c, c - diag, c - diag, thickness, sun);

    make_circle(root, c - core / 2, c - core / 2, core, sun);
}

void draw_rain(lv_obj_t *root, lv_coord_t size)
{
    lv_color_t rain = lv_color_hex(0x55b8ff);
    make_rect(root, size * 4 / 16, size * 11 / 16, 2, size * 4 / 16, rain);
    make_rect(root, size * 7 / 16, size * 12 / 16, 2, size * 3 / 16, rain);
    make_rect(root, size * 10 / 16, size * 11 / 16, 2, size * 4 / 16, rain);
}

void draw_snow(lv_obj_t *root, lv_coord_t size)
{
    lv_color_t snow = lv_color_hex(0xcfe8ff);
    make_circle(root, size * 4 / 16, size * 12 / 16, size * 2 / 16, snow);
    make_circle(root, size * 7 / 16, size * 11 / 16, size * 2 / 16, snow);
    make_circle(root, size * 10 / 16, size * 12 / 16, size * 2 / 16, snow);
}

void draw_storm(lv_obj_t *root, lv_coord_t size)
{
    lv_color_t bolt = lv_color_hex(0xffcf33);
    make_rect(root, size * 7 / 16, size * 10 / 16, 3, size * 3 / 16, bolt);
    make_rect(root, size * 6 / 16, size * 12 / 16, 3, size * 3 / 16, bolt);
}

void draw_fog(lv_obj_t *root, lv_coord_t size)
{
    lv_color_t fog = lv_color_hex(0xaab3bf);
    make_rect(root, size * 3 / 16, size * 10 / 16, size * 10 / 16, 2, fog);
    make_rect(root, size * 2 / 16, size * 13 / 16, size * 11 / 16, 2, fog);
}

}  // namespace

WeatherIconKind weather_icon_kind_from_code(int weather_code)
{
    if (weather_code == 0) return WeatherIconKind::Clear;
    if (weather_code >= 1 && weather_code <= 3) return WeatherIconKind::Cloudy;
    if (weather_code >= 45 && weather_code <= 48) return WeatherIconKind::Fog;
    if (weather_code >= 51 && weather_code <= 67) return WeatherIconKind::Rain;
    if (weather_code >= 71 && weather_code <= 77) return WeatherIconKind::Snow;
    if (weather_code >= 80 && weather_code <= 82) return WeatherIconKind::Rain;
    if (weather_code >= 95) return WeatherIconKind::Storm;
    return WeatherIconKind::Cloudy;
}

lv_obj_t *weather_icon_create(lv_obj_t *parent, WeatherIconKind kind, lv_coord_t size)
{
    lv_obj_t *root = lv_obj_create(parent);
    if (!root) return nullptr;
    style_base(root);
    weather_icon_update(root, kind, size);
    return root;
}

void weather_icon_update(lv_obj_t *icon_root, WeatherIconKind kind, lv_coord_t size)
{
    if (!icon_root || !lv_obj_is_valid(icon_root))
        return;

    lv_obj_clean(icon_root);
    style_base(icon_root);
    lv_obj_set_size(icon_root, size, size);

    lv_color_t cloud = lv_color_hex(0xd7dde6);

    switch (kind) {
        case WeatherIconKind::Clear:
            draw_sun(icon_root, size);
            break;
        case WeatherIconKind::Cloudy:
            draw_cloud(icon_root, size, cloud);
            break;
        case WeatherIconKind::Rain:
            draw_cloud(icon_root, size, cloud);
            draw_rain(icon_root, size);
            break;
        case WeatherIconKind::Snow:
            draw_cloud(icon_root, size, cloud);
            draw_snow(icon_root, size);
            break;
        case WeatherIconKind::Storm:
            draw_cloud(icon_root, size, cloud);
            draw_storm(icon_root, size);
            break;
        case WeatherIconKind::Fog:
            draw_cloud(icon_root, size, cloud);
            draw_fog(icon_root, size);
            break;
    }
}

namespace {

void light_rect(lv_layer_t *layer, lv_coord_t x, lv_coord_t y, lv_coord_t w, lv_coord_t h,
                lv_color_t color, bool round = false)
{
    lv_draw_rect_dsc_t dsc;
    lv_draw_rect_dsc_init(&dsc);
    dsc.bg_color = color;
    dsc.bg_opa = LV_OPA_COVER;
    dsc.radius = round ? LV_RADIUS_CIRCLE : 0;
    lv_area_t area = {x, y, static_cast<lv_coord_t>(x + w - 1), static_cast<lv_coord_t>(y + h - 1)};
    lv_draw_rect(layer, &dsc, &area);
}

void light_line(lv_layer_t *layer, lv_coord_t x1, lv_coord_t y1, lv_coord_t x2, lv_coord_t y2,
                lv_coord_t width, lv_color_t color)
{
    lv_draw_line_dsc_t dsc;
    lv_draw_line_dsc_init(&dsc);
    dsc.p1 = {x1, y1};
    dsc.p2 = {x2, y2};
    dsc.width = width;
    dsc.color = color;
    dsc.opa = LV_OPA_COVER;
    dsc.round_start = 1;
    dsc.round_end = 1;
    lv_draw_line(layer, &dsc);
}

void lightweight_icon_draw(lv_event_t *e)
{
    lv_obj_t *obj = lv_event_get_target_obj(e);
    lv_layer_t *layer = lv_event_get_layer(e);
    const WeatherIconKind kind = static_cast<WeatherIconKind>(
        reinterpret_cast<intptr_t>(lv_obj_get_user_data(obj)) - 1);
    lv_area_t a;
    lv_obj_get_coords(obj, &a);
    const lv_coord_t s = lv_obj_get_width(obj);
    const lv_coord_t x = a.x1;
    const lv_coord_t y = a.y1;
    const lv_coord_t stroke = LV_MAX(2, s / 12);

    if (kind == WeatherIconKind::Clear) {
        const lv_coord_t c = s / 2;
        const lv_coord_t r = s / 6;
        const lv_coord_t ray = s * 2 / 5;
        const lv_color_t sun = lv_color_hex(0xf4c542);
        light_rect(layer, x + c - r, y + c - r, r * 2, r * 2, sun, true);
        static const int8_t dx[] = {0, 7, 10, 7, 0, -7, -10, -7};
        static const int8_t dy[] = {-10, -7, 0, 7, 10, 7, 0, -7};
        for (int i = 0; i < 8; ++i)
            light_line(layer, x + c + dx[i] * (r + 2) / 10, y + c + dy[i] * (r + 2) / 10,
                       x + c + dx[i] * ray / 10, y + c + dy[i] * ray / 10, stroke, sun);
        return;
    }

    const lv_color_t cloud = lv_color_hex(0xd7dde6);
    light_rect(layer, x + s * 2 / 16, y + s * 7 / 16, s * 12 / 16, s * 5 / 16, cloud, true);
    light_rect(layer, x + s * 4 / 16, y + s * 4 / 16, s * 6 / 16, s * 7 / 16, cloud, true);

    if (kind == WeatherIconKind::Rain) {
        const lv_color_t rain = lv_color_hex(0x55b8ff);
        for (int i = 0; i < 3; ++i)
            light_line(layer, x + s * (5 + i * 3) / 16, y + s * 12 / 16,
                       x + s * (4 + i * 3) / 16, y + s * 15 / 16, stroke, rain);
    } else if (kind == WeatherIconKind::Snow) {
        for (int i = 0; i < 3; ++i)
            light_rect(layer, x + s * (4 + i * 4) / 16, y + s * 13 / 16,
                       stroke, stroke, lv_color_hex(0xcfe8ff), true);
    } else if (kind == WeatherIconKind::Storm) {
        light_line(layer, x + s * 9 / 16, y + s * 11 / 16, x + s * 6 / 16,
                   y + s * 15 / 16, stroke + 1, lv_color_hex(0xffcf33));
    } else if (kind == WeatherIconKind::Fog) {
        const lv_color_t fog = lv_color_hex(0xaab3bf);
        light_line(layer, x + s * 2 / 16, y + s * 13 / 16, x + s * 13 / 16, y + s * 13 / 16, stroke, fog);
        light_line(layer, x + s * 3 / 16, y + s * 15 / 16, x + s * 12 / 16, y + s * 15 / 16, stroke, fog);
    }
}

}  // namespace

lv_obj_t *weather_icon_create_lightweight(lv_obj_t *parent, WeatherIconKind kind, lv_coord_t size)
{
    lv_obj_t *icon = lv_obj_create(parent);
    if (!icon) return nullptr;
    style_base(icon);
    lv_obj_add_event_cb(icon, lightweight_icon_draw, LV_EVENT_DRAW_MAIN, nullptr);
    weather_icon_update_lightweight(icon, kind, size);
    return icon;
}

void weather_icon_update_lightweight(lv_obj_t *icon_root, WeatherIconKind kind, lv_coord_t size)
{
    if (!icon_root || !lv_obj_is_valid(icon_root)) return;
    lv_obj_set_user_data(icon_root, reinterpret_cast<void *>(static_cast<intptr_t>(kind) + 1));
    lv_obj_set_size(icon_root, size, size);
    lv_obj_invalidate(icon_root);
}
