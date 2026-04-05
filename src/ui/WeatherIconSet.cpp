#include "ui/WeatherIconSet.h"

namespace {

void style_base(lv_obj_t *obj)
{
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_border_width(obj, 0, 0);
    lv_obj_set_style_radius(obj, 0, 0);
    lv_obj_set_style_pad_all(obj, 0, 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_TRANSP, 0);
}

lv_obj_t *make_rect(lv_obj_t *parent, lv_coord_t x, lv_coord_t y, lv_coord_t w, lv_coord_t h, lv_color_t color, lv_opa_t opa = LV_OPA_COVER)
{
    lv_obj_t *obj = lv_obj_create(parent);
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
    lv_obj_set_style_radius(obj, LV_RADIUS_CIRCLE, 0);
    return obj;
}

lv_obj_t *make_ray(lv_obj_t *parent,
                   lv_coord_t center_x,
                   lv_coord_t center_y,
                   lv_coord_t length,
                   lv_coord_t thickness,
                   int angle_deg_tenths,
                   lv_color_t color)
{
    lv_obj_t *obj = make_rect(parent, center_x - thickness / 2, center_y - length, thickness, length, color);
    lv_obj_set_style_radius(obj, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_transform_pivot_x(obj, thickness / 2, 0);
    lv_obj_set_style_transform_pivot_y(obj, length, 0);
    lv_obj_set_style_transform_rotation(obj, angle_deg_tenths, 0);
    return obj;
}

void draw_cloud(lv_obj_t *root, lv_coord_t size, lv_color_t color)
{
    lv_coord_t y = size * 5 / 16;
    make_circle(root, size * 2 / 16, y + size * 2 / 16, size * 5 / 16, color);
    make_circle(root, size * 5 / 16, y, size * 6 / 16, color);
    make_circle(root, size * 9 / 16, y + size * 2 / 16, size * 5 / 16, color);
    // Build the lower cloud body with circles so no flat rectangle edge is visible.
    make_circle(root, size * 4 / 16, y + size * 4 / 16, size * 4 / 16, color);
    make_circle(root, size * 8 / 16, y + size * 4 / 16, size * 4 / 16, color);
}

void draw_sun(lv_obj_t *root, lv_coord_t size)
{
    lv_color_t sun = lv_color_hex(0xf4c542);
    lv_coord_t c = size / 2;
    lv_coord_t core = size * 6 / 16;
    lv_coord_t ray = size * 4 / 16;
    lv_coord_t thickness = LV_MAX(2, size / 18);
    make_circle(root, c - core / 2, c - core / 2, core, sun);

    make_ray(root, c, c, ray, thickness, 0, sun);
    make_ray(root, c, c, ray, thickness, 450, sun);
    make_ray(root, c, c, ray, thickness, 900, sun);
    make_ray(root, c, c, ray, thickness, 1350, sun);
    make_ray(root, c, c, ray, thickness, 1800, sun);
    make_ray(root, c, c, ray, thickness, 2250, sun);
    make_ray(root, c, c, ray, thickness, 2700, sun);
    make_ray(root, c, c, ray, thickness, 3150, sun);
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
