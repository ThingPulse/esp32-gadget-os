#include "apps/PlanesApp.h"

#include "AppRegistry.h"
#include "services/FileSystemService.h"
#include "services/PlanesService.h"
#include "ui/UiNav.h"
#include "ui/StatusIcons.h"
#include "ui/UiSessionState.h"
#include "ui/UiTrace.h"

#include <cmath>
#include <cstdio>
#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

#if defined(ARDUINO) && defined(ESP_PLATFORM)
#include <LovyanGFX.hpp>
#include <OpenStreetMap-esp32.hpp>
#else
#define STB_IMAGE_IMPLEMENTATION
#include "third_party/stb_image.h"
#endif

APP_REGISTER_COLOR_RANK("Planes", LV_SYMBOL_GPS, planes_app_launch, 0x339af0, 20);

namespace {

static float haversine_km(double lat1, double lon1, double lat2, double lon2)
{
    constexpr double R = 6371.0;
    constexpr double D2R = 3.14159265358979323846 / 180.0;
    double dlat = (lat2 - lat1) * D2R;
    double dlon = (lon2 - lon1) * D2R;
    double a = std::sin(dlat / 2) * std::sin(dlat / 2) +
               std::cos(lat1 * D2R) * std::cos(lat2 * D2R) *
               std::sin(dlon / 2) * std::sin(dlon / 2);
    return (float)(R * 2.0 * std::atan2(std::sqrt(a), std::sqrt(1.0 - a)));
}

struct PlanesAppCtx {
  lv_obj_t *screen = nullptr;
  lv_obj_t *content = nullptr;
  lv_obj_t *list_view = nullptr;
  lv_obj_t *map_view = nullptr;
  lv_obj_t *map_toggle_btn = nullptr;
  lv_obj_t *map_toggle_label = nullptr;
  lv_obj_t *status_label = nullptr;
  lv_obj_t *count_label = nullptr;
  lv_obj_t *map_hint_label = nullptr;
  lv_obj_t *map_summary_label = nullptr;
  lv_obj_t *map_square = nullptr;
  lv_obj_t *map_bg = nullptr;
  lv_obj_t *map_circle = nullptr;
  lv_obj_t *map_center_dot = nullptr;
  lv_obj_t *map_direction = nullptr;
  lv_obj_t *map_north_label = nullptr;
  lv_obj_t *map_plane_dots[PlanesService::MAX_PLANES]{};
  lv_point_precise_t map_direction_points[2]{};
  lv_obj_t *rows[PlanesService::MAX_PLANES]{};
  lv_obj_t *callsign_labels[PlanesService::MAX_PLANES]{};
  lv_obj_t *detail_labels[PlanesService::MAX_PLANES]{};
  lv_timer_t *refresh_timer = nullptr;
  bool closing = false;
  bool map_mode = false;
  lv_image_dsc_t map_bg_dsc{};
  std::vector<uint16_t> map_bg_pixels{};
  std::string map_cache_sig{};
  bool map_tiles_ready = false;
};

static PlanesAppCtx app_ctx;

#if defined(ARDUINO) && defined(ESP_PLATFORM) && defined(COLOR_KIT_PRO)
static inline void swap_rgb565_bytes(uint16_t *pixels, size_t pixel_count)
{
  for (size_t i = 0; i < pixel_count; ++i) {
    const uint16_t px = pixels[i];
    pixels[i] = (uint16_t)((px << 8) | (px >> 8));
  }
}
#endif

static void nav_back_event(lv_event_t *e)
{
  if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
    UI_TRACE("Planes", "back clicked");
    PlanesService::instance().pause();
    app_ctx.closing = true;
    if (app_ctx.refresh_timer) {
      lv_timer_delete(app_ctx.refresh_timer);
      app_ctx.refresh_timer = nullptr;
      UI_TRACE("Planes", "back deleted refresh timer");
    }
    UiSessionState::set_route("home");
    lv_obj_t *target_scr = (lv_obj_t *)lv_event_get_user_data(e);
    UI_TRACE("Planes", "back target valid=%d", (target_scr && lv_obj_is_valid(target_scr)) ? 1 : 0);
    if (!target_scr || !lv_obj_is_valid(target_scr)) return;
    ui_load_screen(target_scr, true);
  }
}

static void planes_app_apply_view_mode()
{
  if (!app_ctx.list_view || !lv_obj_is_valid(app_ctx.list_view)) return;
  if (!app_ctx.map_view || !lv_obj_is_valid(app_ctx.map_view)) return;
  if (!app_ctx.map_toggle_label || !lv_obj_is_valid(app_ctx.map_toggle_label)) return;

  if (app_ctx.map_mode) {
    lv_obj_add_flag(app_ctx.list_view, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(app_ctx.map_view, LV_OBJ_FLAG_HIDDEN);
    lv_label_set_text(app_ctx.map_toggle_label, "List");
  } else {
    lv_obj_clear_flag(app_ctx.list_view, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(app_ctx.map_view, LV_OBJ_FLAG_HIDDEN);
    lv_label_set_text(app_ctx.map_toggle_label, "Map");
  }
}

static void planes_app_update_map_layout()
{
  if (!app_ctx.map_view || !lv_obj_is_valid(app_ctx.map_view)) return;
  if (!app_ctx.map_square || !lv_obj_is_valid(app_ctx.map_square)) return;
  if (!app_ctx.map_hint_label || !lv_obj_is_valid(app_ctx.map_hint_label)) return;
  if (!app_ctx.map_summary_label || !lv_obj_is_valid(app_ctx.map_summary_label)) return;

  lv_obj_update_layout(app_ctx.map_view);
  lv_coord_t w = lv_obj_get_content_width(app_ctx.map_view);
  lv_coord_t h = lv_obj_get_content_height(app_ctx.map_view);
  if (w <= 0 || h <= 0) return;

  const lv_coord_t map_size = LV_MIN(w, h);
  lv_obj_set_size(app_ctx.map_square, map_size, map_size);

  if (w > h) {
    lv_obj_set_flex_flow(app_ctx.map_view, LV_FLEX_FLOW_ROW);
    lv_obj_set_width(app_ctx.map_hint_label, w - map_size - 8);
    lv_obj_set_width(app_ctx.map_summary_label, w - map_size - 8);
  } else {
    lv_obj_set_flex_flow(app_ctx.map_view, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_width(app_ctx.map_hint_label, map_size);
    lv_obj_set_width(app_ctx.map_summary_label, map_size);
  }
}

static float km_per_lon_degree(double latitude_deg)
{
  constexpr double D2R = 3.14159265358979323846 / 180.0;
  return (float)(111.320 * std::cos(latitude_deg * D2R));
}

static constexpr const char *PLANES_MAP_CACHE_DIR = "/cache";

#if defined(ARDUINO) && defined(ESP_PLATFORM)
static constexpr const char *PLANES_MAP_CACHE_META = "/cache/planes_map.meta";
static constexpr const char *PLANES_MAP_CACHE_RGB565 = "/cache/planes_map.rgb565";

static uint8_t choose_osm_zoom(double latitude_deg, double range_km, lv_coord_t map_px)
{
  const double cos_lat = std::max(0.15, std::cos(latitude_deg * 3.14159265358979323846 / 180.0));
  const double width_m = std::max(1000.0, range_km * 2000.0 * 1.15);
  const double px = std::max(64.0, (double)map_px);
  const double raw_zoom = std::log2((156543.03392 * cos_lat * px) / width_m);
  const int z = (int)std::lround(raw_zoom);
  return (uint8_t)LV_CLAMP(1, z, 18);
}

static std::string map_signature(const PlanesService &svc, lv_coord_t square_size, uint8_t zoom)
{
  char sig[128] = {0};
  std::snprintf(sig,
                sizeof(sig),
                "lat=%.5f;lon=%.5f;range=%.2f;size=%d;zoom=%u",
                svc.center_latitude(),
                svc.center_longitude(),
                svc.range_km(),
                (int)square_size,
                (unsigned)zoom);
  return std::string(sig);
}

static bool load_cached_map_rgb565(const std::string &sig, lv_coord_t square_size, std::vector<uint16_t> &pixels_out)
{
  IFileSystem &fs = FileSystemService::instance().fs();
  std::string meta;
  std::string raw;
  if (!fs.read(PLANES_MAP_CACHE_META, meta) || meta != sig) return false;
  if (!fs.read(PLANES_MAP_CACHE_RGB565, raw)) return false;
  const size_t expected_bytes = (size_t)square_size * (size_t)square_size * sizeof(uint16_t);
  if (raw.size() != expected_bytes) return false;
  pixels_out.resize(expected_bytes / sizeof(uint16_t));
  std::memcpy(pixels_out.data(), raw.data(), expected_bytes);
  return true;
}

static void save_cached_map_rgb565(const std::string &sig, const uint16_t *pixels, lv_coord_t square_size)
{
  IFileSystem &fs = FileSystemService::instance().fs();
  (void)fs.mkdir_p(PLANES_MAP_CACHE_DIR);
  const size_t bytes = (size_t)square_size * (size_t)square_size * sizeof(uint16_t);
  const std::string raw((const char *)pixels, bytes);
  (void)fs.write(PLANES_MAP_CACHE_RGB565, raw);
  (void)fs.write(PLANES_MAP_CACHE_META, sig);
}

static bool fetch_map_tiles_rgb565(const PlanesService &svc, lv_coord_t square_size, std::vector<uint16_t> &pixels_out, std::string &sig_out)
{
  static OpenStreetMap osm;
  static bool osm_cache_ready = false;

  const uint8_t zoom = choose_osm_zoom(svc.center_latitude(), svc.range_km(), square_size);
  sig_out = map_signature(svc, square_size, zoom);

  if (load_cached_map_rgb565(sig_out, square_size, pixels_out)) return true;

  if (!osm_cache_ready) {
    const uint16_t tiles = osm.tilesNeeded((uint16_t)square_size, (uint16_t)square_size);
    if (!osm.resizeTilesCache(tiles)) return false;
    osm_cache_ready = true;
  }

  osm.setSize((uint16_t)square_size, (uint16_t)square_size);
  LGFX_Sprite map_sprite;
  if (!osm.fetchMap(map_sprite,
                    svc.center_longitude(),
                    svc.center_latitude(),
                    zoom,
                    2500)) {
    return false;
  }

  uint16_t *buf = (uint16_t *)map_sprite.getBuffer();
  const size_t px_count = (size_t)square_size * (size_t)square_size;
  if (!buf) return false;

  pixels_out.assign(buf, buf + px_count);
  save_cached_map_rgb565(sig_out, pixels_out.data(), square_size);
  map_sprite.deleteSprite();
  return true;
}

static void set_map_bg_image(lv_obj_t *img_obj, lv_image_dsc_t &dsc, std::vector<uint16_t> &pixels, lv_coord_t square_size)
{
  if (!img_obj || !lv_obj_is_valid(img_obj) || pixels.empty()) return;
#if defined(ARDUINO) && defined(ESP_PLATFORM) && defined(COLOR_KIT_PRO)
  swap_rgb565_bytes(pixels.data(), pixels.size());
#endif
  std::memset(&dsc, 0, sizeof(dsc));
  dsc.header.w = (uint32_t)square_size;
  dsc.header.h = (uint32_t)square_size;
  dsc.header.cf = LV_COLOR_FORMAT_RGB565;
  dsc.data_size = pixels.size() * sizeof(uint16_t);
  dsc.data = (const uint8_t *)pixels.data();
  lv_image_set_src(img_obj, &dsc);
}

static void planes_app_ensure_map_tiles(const PlanesService &svc, lv_coord_t square_size)
{
  if (!app_ctx.map_bg || !lv_obj_is_valid(app_ctx.map_bg)) return;
  const uint8_t zoom = choose_osm_zoom(svc.center_latitude(), svc.range_km(), square_size);
  const std::string sig = map_signature(svc, square_size, zoom);
  if (app_ctx.map_tiles_ready && sig == app_ctx.map_cache_sig) return;

  std::vector<uint16_t> pixels;
  std::string loaded_sig;
  if (fetch_map_tiles_rgb565(svc, square_size, pixels, loaded_sig)) {
    app_ctx.map_bg_pixels = std::move(pixels);
    app_ctx.map_cache_sig = loaded_sig;
    app_ctx.map_tiles_ready = true;
    set_map_bg_image(app_ctx.map_bg, app_ctx.map_bg_dsc, app_ctx.map_bg_pixels, square_size);
  } else {
    app_ctx.map_tiles_ready = false;
    app_ctx.map_bg_pixels.clear();
  }
}
#endif

#if !defined(ARDUINO) || !defined(ESP_PLATFORM)
static constexpr const char *PLANES_MAP_CACHE_NATIVE_META = "/cache/planes_map_native.meta";
static constexpr const char *PLANES_MAP_CACHE_NATIVE_RGB565 = "/cache/planes_map_native.rgb565";

static uint8_t choose_osm_zoom(double latitude_deg, double range_km, lv_coord_t map_px)
{
  const double cos_lat = std::max(0.15, std::cos(latitude_deg * 3.14159265358979323846 / 180.0));
  const double width_m = std::max(1000.0, range_km * 2000.0 * 1.15);
  const double px = std::max(64.0, (double)map_px);
  const double raw_zoom = std::log2((156543.03392 * cos_lat * px) / width_m);
  const int z = (int)std::lround(raw_zoom);
  return (uint8_t)LV_CLAMP(1, z, 18);
}

static std::string map_signature(const PlanesService &svc, lv_coord_t square_size, uint8_t zoom)
{
  char sig[128] = {0};
  std::snprintf(sig,
                sizeof(sig),
                "native;lat=%.5f;lon=%.5f;range=%.2f;size=%d;zoom=%u",
                svc.center_latitude(),
                svc.center_longitude(),
                svc.range_km(),
                (int)square_size,
                (unsigned)zoom);
  return std::string(sig);
}

static bool load_cached_map_rgb565(const std::string &sig, lv_coord_t square_size, std::vector<uint16_t> &pixels_out)
{
  IFileSystem &fs = FileSystemService::instance().fs();
  std::string meta;
  std::string raw;
  if (!fs.read(PLANES_MAP_CACHE_NATIVE_META, meta) || meta != sig) return false;
  if (!fs.read(PLANES_MAP_CACHE_NATIVE_RGB565, raw)) return false;
  const size_t expected_bytes = (size_t)square_size * (size_t)square_size * sizeof(uint16_t);
  if (raw.size() != expected_bytes) return false;
  pixels_out.resize(expected_bytes / sizeof(uint16_t));
  std::memcpy(pixels_out.data(), raw.data(), expected_bytes);
  return true;
}

static void save_cached_map_rgb565(const std::string &sig, const uint16_t *pixels, lv_coord_t square_size)
{
  IFileSystem &fs = FileSystemService::instance().fs();
  (void)fs.mkdir_p(PLANES_MAP_CACHE_DIR);
  const size_t bytes = (size_t)square_size * (size_t)square_size * sizeof(uint16_t);
  const std::string raw((const char *)pixels, bytes);
  (void)fs.write(PLANES_MAP_CACHE_NATIVE_RGB565, raw);
  (void)fs.write(PLANES_MAP_CACHE_NATIVE_META, sig);
}

static std::string shell_escape_single(const std::string &s)
{
  std::string out;
  out.reserve(s.size() + 8);
  out.push_back('\'');
  for (char c : s) {
    if (c == '\'') out += "'\\''";
    else out.push_back(c);
  }
  out.push_back('\'');
  return out;
}

static bool curl_get_binary(const std::string &url, std::string &out)
{
  const std::string cmd = "curl -L --max-time 8 -s " + shell_escape_single(url);
  FILE *pipe = popen(cmd.c_str(), "r");
  if (!pipe) return false;
  out.clear();
  char buf[4096];
  while (true) {
    size_t n = fread(buf, 1, sizeof(buf), pipe);
    if (n > 0) out.append(buf, n);
    if (n < sizeof(buf)) break;
  }
  const int rc = pclose(pipe);
  return rc == 0 && !out.empty();
}

static void lon_lat_to_world_px(double lon, double lat, int z, double &x, double &y)
{
  const double n = std::pow(2.0, z) * 256.0;
  x = (lon + 180.0) / 360.0 * n;
  const double lat_rad = lat * 3.14159265358979323846 / 180.0;
  y = (1.0 - std::log(std::tan(lat_rad) + 1.0 / std::cos(lat_rad)) / 3.14159265358979323846) * 0.5 * n;
}

static uint16_t rgb888_to_565(uint8_t r, uint8_t g, uint8_t b)
{
  return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

static bool fetch_map_tiles_rgb565(const PlanesService &svc, lv_coord_t square_size, std::vector<uint16_t> &pixels_out, std::string &sig_out)
{
  const uint8_t zoom = choose_osm_zoom(svc.center_latitude(), svc.range_km(), square_size);
  sig_out = map_signature(svc, square_size, zoom);
  if (load_cached_map_rgb565(sig_out, square_size, pixels_out)) return true;

  const int tile_px = 256;
  const int n = 1 << zoom;
  pixels_out.assign((size_t)square_size * (size_t)square_size, rgb888_to_565(17, 19, 31));

  double center_x = 0.0;
  double center_y = 0.0;
  lon_lat_to_world_px(svc.center_longitude(), svc.center_latitude(), zoom, center_x, center_y);
  const double start_x = center_x - (double)square_size / 2.0;
  const double start_y = center_y - (double)square_size / 2.0;
  const int tx0 = (int)std::floor(start_x / tile_px);
  const int ty0 = (int)std::floor(start_y / tile_px);
  const int tx1 = (int)std::floor((start_x + square_size - 1) / tile_px);
  const int ty1 = (int)std::floor((start_y + square_size - 1) / tile_px);

  bool any_tile = false;
  for (int ty = ty0; ty <= ty1; ++ty) {
    if (ty < 0 || ty >= n) continue;
    for (int tx = tx0; tx <= tx1; ++tx) {
      int wrapped_tx = tx % n;
      if (wrapped_tx < 0) wrapped_tx += n;
      const std::string url = "https://tile.openstreetmap.org/" + std::to_string((int)zoom) + "/" +
                              std::to_string(wrapped_tx) + "/" + std::to_string(ty) + ".png";
      std::string png;
      if (!curl_get_binary(url, png)) continue;

      int w = 0, h = 0, comp = 0;
      unsigned char *img = stbi_load_from_memory((const stbi_uc *)png.data(), (int)png.size(), &w, &h, &comp, 3);
      if (!img || w != tile_px || h != tile_px) {
        if (img) stbi_image_free(img);
        continue;
      }

      const int tile_off_x = tx * tile_px - (int)std::floor(start_x);
      const int tile_off_y = ty * tile_px - (int)std::floor(start_y);
      for (int py = 0; py < tile_px; ++py) {
        const int dst_y = tile_off_y + py;
        if (dst_y < 0 || dst_y >= square_size) continue;
        for (int px = 0; px < tile_px; ++px) {
          const int dst_x = tile_off_x + px;
          if (dst_x < 0 || dst_x >= square_size) continue;
          const size_t src_i = (size_t)(py * tile_px + px) * 3;
          pixels_out[(size_t)dst_y * (size_t)square_size + (size_t)dst_x] =
              rgb888_to_565(img[src_i], img[src_i + 1], img[src_i + 2]);
        }
      }
      stbi_image_free(img);
      any_tile = true;
    }
  }

  if (!any_tile) return false;
  save_cached_map_rgb565(sig_out, pixels_out.data(), square_size);
  return true;
}

static void set_map_bg_image(lv_obj_t *img_obj, lv_image_dsc_t &dsc, std::vector<uint16_t> &pixels, lv_coord_t square_size)
{
  if (!img_obj || !lv_obj_is_valid(img_obj) || pixels.empty()) return;
#if defined(ARDUINO) && defined(ESP_PLATFORM) && defined(COLOR_KIT_PRO)
  swap_rgb565_bytes(pixels.data(), pixels.size());
#endif
  std::memset(&dsc, 0, sizeof(dsc));
  dsc.header.w = (uint32_t)square_size;
  dsc.header.h = (uint32_t)square_size;
  dsc.header.cf = LV_COLOR_FORMAT_RGB565;
  dsc.data_size = pixels.size() * sizeof(uint16_t);
  dsc.data = (const uint8_t *)pixels.data();
  lv_image_set_src(img_obj, &dsc);
}

static void planes_app_ensure_map_tiles(const PlanesService &svc, lv_coord_t square_size)
{
  if (!app_ctx.map_bg || !lv_obj_is_valid(app_ctx.map_bg)) return;
  const uint8_t zoom = choose_osm_zoom(svc.center_latitude(), svc.range_km(), square_size);
  const std::string sig = map_signature(svc, square_size, zoom);
  if (app_ctx.map_tiles_ready && sig == app_ctx.map_cache_sig) return;

  std::vector<uint16_t> pixels;
  std::string loaded_sig;
  if (fetch_map_tiles_rgb565(svc, square_size, pixels, loaded_sig)) {
    app_ctx.map_bg_pixels = std::move(pixels);
    app_ctx.map_cache_sig = loaded_sig;
    app_ctx.map_tiles_ready = true;
    set_map_bg_image(app_ctx.map_bg, app_ctx.map_bg_dsc, app_ctx.map_bg_pixels, square_size);
  } else {
    app_ctx.map_tiles_ready = false;
    app_ctx.map_bg_pixels.clear();
  }
}
#endif

static void planes_app_update_map_ui()
{
  if (!app_ctx.map_square || !lv_obj_is_valid(app_ctx.map_square)) return;
  if (!app_ctx.map_circle || !lv_obj_is_valid(app_ctx.map_circle)) return;
  if (!app_ctx.map_center_dot || !lv_obj_is_valid(app_ctx.map_center_dot)) return;
  if (!app_ctx.map_direction || !lv_obj_is_valid(app_ctx.map_direction)) return;
  if (!app_ctx.map_north_label || !lv_obj_is_valid(app_ctx.map_north_label)) return;
  if (!app_ctx.map_hint_label || !lv_obj_is_valid(app_ctx.map_hint_label)) return;
  if (!app_ctx.map_summary_label || !lv_obj_is_valid(app_ctx.map_summary_label)) return;

  planes_app_update_map_layout();
  lv_obj_update_layout(app_ctx.map_square);
  const lv_coord_t square_size = lv_obj_get_width(app_ctx.map_square);
  if (square_size <= 20) return;

  const lv_coord_t circle_d = square_size - 16;
  const lv_coord_t circle_x = (square_size - circle_d) / 2;
  const lv_coord_t circle_y = (square_size - circle_d) / 2;
  const lv_coord_t center_x = square_size / 2;
  const lv_coord_t center_y = square_size / 2;
  const float radius_px = (float)circle_d * 0.5f;

  lv_obj_set_size(app_ctx.map_circle, circle_d, circle_d);
  lv_obj_set_pos(app_ctx.map_circle, circle_x, circle_y);
  lv_obj_set_size(app_ctx.map_center_dot, 8, 8);
  lv_obj_set_pos(app_ctx.map_center_dot, center_x - 4, center_y - 4);
  lv_obj_set_pos(app_ctx.map_north_label, center_x - 4, circle_y - 16);

  app_ctx.map_direction_points[0] = {center_x, center_y};
  app_ctx.map_direction_points[1] = {center_x, (lv_coord_t)(center_y - radius_px)};
  lv_line_set_points(app_ctx.map_direction, app_ctx.map_direction_points, 2);

  PlanesService &svc = PlanesService::instance();
  if (!svc.is_configured()) {
    lv_label_set_text(app_ctx.map_summary_label, "Map unavailable: configure center/range in Settings > Planes.");
    lv_label_set_text(app_ctx.map_hint_label, "Line points north. Circle = configured search range.");
    for (int i = 0; i < PlanesService::MAX_PLANES; ++i)
      if (app_ctx.map_plane_dots[i] && lv_obj_is_valid(app_ctx.map_plane_dots[i]))
        lv_obj_add_flag(app_ctx.map_plane_dots[i], LV_OBJ_FLAG_HIDDEN);
    return;
  }

  planes_app_ensure_map_tiles(svc, square_size);

  const float range_km = (float)LV_MAX(0.1, svc.range_km());
  const float km_lat = 111.32f;
  const float km_lon = LV_MAX(1.0f, km_per_lon_degree(svc.center_latitude()));
  const PlaneState *planes = svc.planes();
  const int count = svc.plane_count();
  int visible = 0;

  for (int i = 0; i < PlanesService::MAX_PLANES; ++i) {
    if (!app_ctx.map_plane_dots[i] || !lv_obj_is_valid(app_ctx.map_plane_dots[i])) continue;
    if (i >= count || !planes[i].valid || (planes[i].on_ground && svc.hide_ground())) {
      lv_obj_add_flag(app_ctx.map_plane_dots[i], LV_OBJ_FLAG_HIDDEN);
      continue;
    }

    const float north_km = (planes[i].latitude - (float)svc.center_latitude()) * km_lat;
    const float east_km  = (planes[i].longitude - (float)svc.center_longitude()) * km_lon;
    const float nx = east_km / range_km;
    const float ny = north_km / range_km;
    if ((nx * nx + ny * ny) > 1.0f) {
      lv_obj_add_flag(app_ctx.map_plane_dots[i], LV_OBJ_FLAG_HIDDEN);
      continue;
    }

    lv_obj_clear_flag(app_ctx.map_plane_dots[i], LV_OBJ_FLAG_HIDDEN);
    const lv_coord_t px = (lv_coord_t)(center_x + nx * radius_px);
    const lv_coord_t py = (lv_coord_t)(center_y - ny * radius_px);
    lv_obj_set_pos(app_ctx.map_plane_dots[i], px - 3, py - 3);
    ++visible;
  }

  char summary[128] = {0};
  std::snprintf(summary,
                sizeof(summary),
                "Center %.3f, %.3f\nRange %.0f km  Visible %d/%d",
                svc.center_latitude(),
                svc.center_longitude(),
                svc.range_km(),
                visible,
                count);
  lv_label_set_text(app_ctx.map_summary_label, summary);
#if defined(ARDUINO) && defined(ESP_PLATFORM)
  lv_label_set_text(app_ctx.map_hint_label, "OSM tiles cached in flash and reused until center/range/size changes.");
#else
  lv_label_set_text(app_ctx.map_hint_label, "OSM tiles cached locally and reused until center/range/size changes.");
#endif
}

static void planes_app_update_ui()
{
  if (app_ctx.closing) return;
  if (!app_ctx.screen || !lv_obj_is_valid(app_ctx.screen)) return;
  if (!app_ctx.status_label || !lv_obj_is_valid(app_ctx.status_label)) return;
  if (!app_ctx.count_label || !lv_obj_is_valid(app_ctx.count_label)) return;

  PlanesService &svc = PlanesService::instance();

  if (!svc.is_configured()) {
    lv_label_set_text(app_ctx.status_label, "Configure center/range in Settings > Planes");
    lv_label_set_text(app_ctx.count_label, "Planes: --");
    for (int i = 0; i < PlanesService::MAX_PLANES; ++i)
      if (app_ctx.rows[i] && lv_obj_is_valid(app_ctx.rows[i])) lv_obj_add_flag(app_ctx.rows[i], LV_OBJ_FLAG_HIDDEN);
    planes_app_update_map_ui();
    return;
  }

  if (!svc.has_data()) {
    lv_label_set_text(app_ctx.status_label, svc.last_error().empty() ? "No plane data yet" : svc.last_error().c_str());
    {
      char buf[48] = {0};
      const int rl = svc.rate_limit_remaining();
      if (rl >= 0) std::snprintf(buf, sizeof(buf), "Planes: 0  Cr:%d", rl);
      else std::snprintf(buf, sizeof(buf), "Planes: 0");
      lv_label_set_text(app_ctx.count_label, buf);
    }
    for (int i = 0; i < PlanesService::MAX_PLANES; ++i)
      if (app_ctx.rows[i] && lv_obj_is_valid(app_ctx.rows[i])) lv_obj_add_flag(app_ctx.rows[i], LV_OBJ_FLAG_HIDDEN);
    planes_app_update_map_ui();
    return;
  }

  char status_buf[96] = {0};
  std::snprintf(status_buf,
                sizeof(status_buf),
                "Center %.3f, %.3f | Range %.0f km",
                svc.center_latitude(),
                svc.center_longitude(),
                svc.range_km());
  lv_label_set_text(app_ctx.status_label, status_buf);

  char count_buf[64] = {0};
  {
    const int rl = svc.rate_limit_remaining();
    if (rl >= 0)
      std::snprintf(count_buf, sizeof(count_buf), "Planes: %d  Cr:%d", svc.plane_count(), rl);
    else
      std::snprintf(count_buf, sizeof(count_buf), "Planes: %d", svc.plane_count());
  }
  lv_label_set_text(app_ctx.count_label, count_buf);

  const PlaneState *planes = svc.planes();
  const int count = svc.plane_count();
  for (int i = 0; i < PlanesService::MAX_PLANES; ++i) {
    if (!app_ctx.rows[i] || !lv_obj_is_valid(app_ctx.rows[i])) continue;
    if (!app_ctx.callsign_labels[i] || !lv_obj_is_valid(app_ctx.callsign_labels[i])) continue;
    if (!app_ctx.detail_labels[i] || !lv_obj_is_valid(app_ctx.detail_labels[i])) continue;

    if (i >= count || !planes[i].valid) {
      lv_obj_add_flag(app_ctx.rows[i], LV_OBJ_FLAG_HIDDEN);
      continue;
    }

    if (planes[i].on_ground && svc.hide_ground()) {
      lv_obj_add_flag(app_ctx.rows[i], LV_OBJ_FLAG_HIDDEN);
      continue;
    }

    lv_obj_clear_flag(app_ctx.rows[i], LV_OBJ_FLAG_HIDDEN);
    std::string first_line = planes[i].callsign;
    if (!planes[i].route.empty()) {
      first_line += "  ";
      first_line += planes[i].route;
    }
    lv_label_set_text(app_ctx.callsign_labels[i], first_line.c_str());

    if (planes[i].on_ground) {
        lv_label_set_text(app_ctx.detail_labels[i], "On Ground");
    } else {
        const float dist_km = haversine_km(svc.center_latitude(), svc.center_longitude(),
                                           planes[i].latitude, planes[i].longitude);
        char detail_buf[96] = {0};
        if (svc.is_metric()) {
          const float speed_kmh = planes[i].velocity_ms * 3.6f;
          std::snprintf(detail_buf, sizeof(detail_buf),
                  "Alt: %.0fm  %.0f km/h  %.1f km",
                  planes[i].altitude_m, speed_kmh, dist_km);
        } else {
          const float alt_ft    = planes[i].altitude_m * 3.28084f;
          const float speed_mph = planes[i].velocity_ms * 2.23694f;
          const float dist_mi   = dist_km * 0.621371f;
          std::snprintf(detail_buf, sizeof(detail_buf),
                  "Alt: %.0fft  %.0f mph  %.1f mi",
                  alt_ft, speed_mph, dist_mi);
        }
        lv_label_set_text(app_ctx.detail_labels[i], detail_buf);
    }
  }
  planes_app_update_map_ui();
}

static void planes_app_timer_cb(lv_timer_t *timer)
{
  (void)timer;
  if (app_ctx.closing) return;
  planes_app_update_ui();
}

static void planes_app_refresh_event(lv_event_t *e)
{
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  if (app_ctx.closing) return;

  UI_TRACE("Planes", "refresh clicked");

  if (app_ctx.status_label && lv_obj_is_valid(app_ctx.status_label))
    lv_label_set_text(app_ctx.status_label, "Refreshing...");

  PlanesService::instance().refresh_now();
  planes_app_update_ui();
}

static void planes_app_toggle_view_event(lv_event_t *e)
{
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  if (app_ctx.closing) return;
  app_ctx.map_mode = !app_ctx.map_mode;
  planes_app_apply_view_mode();
  planes_app_update_ui();
}

static void planes_app_map_resize_event(lv_event_t *e)
{
  if (lv_event_get_code(e) != LV_EVENT_SIZE_CHANGED) return;
  planes_app_update_map_layout();
  planes_app_update_map_ui();
}

static void planes_app_screen_delete_event(lv_event_t *e)
{
  (void)e;
  UI_TRACE("Planes", "screen delete event");
  PlanesService::instance().pause();
  if (app_ctx.refresh_timer) {
    lv_timer_delete(app_ctx.refresh_timer);
    app_ctx.refresh_timer = nullptr;
  }
  app_ctx = PlanesAppCtx{};
}

}  // namespace

void planes_app_launch(void)
{
  UI_TRACE("Planes", "launch begin");
  app_ctx.closing = false;
  lv_obj_t *home_scr = lv_screen_active();

  lv_obj_t *scr = lv_obj_create(nullptr);
  lv_obj_set_style_bg_color(scr, lv_color_hex(0x1e2030), 0);
  lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
  lv_obj_set_style_pad_all(scr, 0, 0);
  lv_obj_set_style_border_width(scr, 0, 0);
  lv_obj_add_event_cb(scr, planes_app_screen_delete_event, LV_EVENT_DELETE, nullptr);

  lv_obj_t *bar = lv_obj_create(scr);
  lv_obj_set_size(bar, lv_pct(100), 44);
  lv_obj_set_pos(bar, 0, 0);
  lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_color(bar, lv_color_hex(0x14162a), 0);
  lv_obj_set_style_border_width(bar, 0, 0);
  lv_obj_set_style_radius(bar, 0, 0);
  lv_obj_set_style_pad_all(bar, 0, 0);

  lv_obj_t *back_btn = lv_button_create(bar);
  lv_obj_set_size(back_btn, 56, 44);
  lv_obj_align(back_btn, LV_ALIGN_LEFT_MID, 4, 0);
  lv_obj_set_style_bg_opa(back_btn, LV_OPA_TRANSP, 0);
  lv_obj_set_style_shadow_width(back_btn, 0, 0);
  lv_obj_set_style_border_width(back_btn, 0, 0);
  lv_obj_add_event_cb(back_btn, nav_back_event, LV_EVENT_CLICKED, home_scr);

  lv_obj_t *back_label = lv_label_create(back_btn);
  lv_label_set_text(back_label, LV_SYMBOL_LEFT);
  lv_obj_set_style_text_color(back_label, lv_color_white(), 0);
  lv_obj_center(back_label);

  lv_obj_t *title = lv_label_create(bar);
  lv_label_set_text(title, "Planes");
  lv_obj_set_style_text_color(title, lv_color_white(), 0);
  lv_obj_align(title, LV_ALIGN_CENTER, 0, 0);

  lv_obj_t *refresh_btn = lv_button_create(bar);
  lv_obj_set_size(refresh_btn, 70, 32);
  lv_obj_align(refresh_btn, LV_ALIGN_RIGHT_MID, -80, 0);
  lv_obj_add_event_cb(refresh_btn, planes_app_refresh_event, LV_EVENT_CLICKED, nullptr);

  lv_obj_t *refresh_lbl = lv_label_create(refresh_btn);
  lv_label_set_text(refresh_lbl, "Refresh");
  lv_obj_center(refresh_lbl);

  lv_obj_t *map_btn = lv_button_create(bar);
  lv_obj_set_size(map_btn, 62, 32);
  lv_obj_align(map_btn, LV_ALIGN_RIGHT_MID, -10, 0);
  lv_obj_add_event_cb(map_btn, planes_app_toggle_view_event, LV_EVENT_CLICKED, nullptr);

  lv_obj_t *map_lbl = lv_label_create(map_btn);
  lv_label_set_text(map_lbl, "Map");
  lv_obj_center(map_lbl);

  create_time_status_label(bar, 56);
  create_wifi_status_icon(bar, -156);

  lv_obj_t *content = lv_obj_create(scr);
  lv_obj_set_pos(content, 0, 44);
  lv_obj_set_size(content, lv_pct(100), 480 - 44);
  lv_obj_set_style_bg_opa(content, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(content, 0, 0);
  lv_obj_set_style_pad_all(content, 12, 0);
  lv_obj_set_style_pad_row(content, 8, 0);
  lv_obj_set_layout(content, LV_LAYOUT_NONE);

  lv_obj_t *list_view = lv_obj_create(content);
  lv_obj_set_size(list_view, lv_pct(100), lv_pct(100));
  lv_obj_set_style_bg_opa(list_view, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(list_view, 0, 0);
  lv_obj_set_style_pad_all(list_view, 0, 0);
  lv_obj_set_style_pad_row(list_view, 8, 0);
  lv_obj_set_layout(list_view, LV_LAYOUT_FLEX);
  lv_obj_set_flex_flow(list_view, LV_FLEX_FLOW_COLUMN);

  lv_obj_t *status_label = lv_label_create(list_view);
  lv_obj_set_style_text_color(status_label, lv_color_hex(0xb7bdd7), 0);

  lv_obj_t *count_label = lv_label_create(list_view);
  lv_obj_set_style_text_color(count_label, lv_color_white(), 0);
  lv_obj_set_style_text_font(count_label, &lv_font_montserrat_14, 0);

  lv_obj_t *list = lv_obj_create(list_view);
  lv_obj_set_size(list, lv_pct(100), 390);
  lv_obj_set_style_bg_color(list, lv_color_hex(0x2a2d42), 0);
  lv_obj_set_style_bg_opa(list, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(list, 0, 0);
  lv_obj_set_style_radius(list, 10, 0);
  lv_obj_set_style_pad_all(list, 8, 0);
  lv_obj_set_style_pad_row(list, 6, 0);
  lv_obj_set_layout(list, LV_LAYOUT_FLEX);
  lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);

  for (int i = 0; i < PlanesService::MAX_PLANES; ++i) {
    lv_obj_t *row = lv_obj_create(list);
    lv_obj_set_size(row, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_pad_all(row, 0, 0);
    lv_obj_set_style_pad_row(row, 2, 0);
    lv_obj_set_layout(row, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_COLUMN);

    lv_obj_t *callsign = lv_label_create(row);
    lv_obj_set_style_text_color(callsign, lv_color_white(), 0);
    lv_obj_set_style_text_font(callsign, &lv_font_montserrat_14, 0);

    lv_obj_t *detail = lv_label_create(row);
    lv_obj_set_width(detail, lv_pct(100));
    lv_label_set_long_mode(detail, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_color(detail, lv_color_hex(0xb7bdd7), 0);
    lv_obj_set_style_text_font(detail, &lv_font_montserrat_14, 0);

    app_ctx.rows[i] = row;
    app_ctx.callsign_labels[i] = callsign;
    app_ctx.detail_labels[i] = detail;
  }

  lv_obj_t *map_view = lv_obj_create(content);
  lv_obj_set_size(map_view, lv_pct(100), lv_pct(100));
  lv_obj_set_style_bg_opa(map_view, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(map_view, 0, 0);
  lv_obj_set_style_pad_all(map_view, 0, 0);
  lv_obj_set_style_pad_row(map_view, 8, 0);
  lv_obj_set_style_pad_column(map_view, 8, 0);
  lv_obj_set_layout(map_view, LV_LAYOUT_FLEX);
  lv_obj_set_flex_flow(map_view, LV_FLEX_FLOW_COLUMN);
  lv_obj_add_flag(map_view, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_event_cb(map_view, planes_app_map_resize_event, LV_EVENT_SIZE_CHANGED, nullptr);

  lv_obj_t *map_square = lv_obj_create(map_view);
  lv_obj_set_size(map_square, 300, 300);
  lv_obj_set_style_bg_color(map_square, lv_color_hex(0x11131f), 0);
  lv_obj_set_style_bg_opa(map_square, LV_OPA_COVER, 0);
  lv_obj_set_style_border_color(map_square, lv_color_hex(0x3f4560), 0);
  lv_obj_set_style_border_width(map_square, 1, 0);
  lv_obj_set_style_radius(map_square, 10, 0);
  lv_obj_set_style_pad_all(map_square, 0, 0);
  lv_obj_clear_flag(map_square, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t *map_bg = lv_image_create(map_square);
  lv_obj_set_size(map_bg, lv_pct(100), lv_pct(100));
  lv_obj_set_pos(map_bg, 0, 0);

  lv_obj_t *map_circle = lv_obj_create(map_square);
  lv_obj_set_style_bg_opa(map_circle, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_color(map_circle, lv_color_hex(0x4dabf7), 0);
  lv_obj_set_style_border_width(map_circle, 2, 0);
  lv_obj_set_style_radius(map_circle, LV_RADIUS_CIRCLE, 0);
  lv_obj_clear_flag(map_circle, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t *map_center_dot = lv_obj_create(map_square);
  lv_obj_set_style_bg_color(map_center_dot, lv_color_hex(0xffffff), 0);
  lv_obj_set_style_bg_opa(map_center_dot, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(map_center_dot, 0, 0);
  lv_obj_set_style_radius(map_center_dot, LV_RADIUS_CIRCLE, 0);
  lv_obj_clear_flag(map_center_dot, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t *map_direction = lv_line_create(map_square);
  app_ctx.map_direction_points[0] = {150, 150};
  app_ctx.map_direction_points[1] = {150, 16};
  lv_line_set_points(map_direction, app_ctx.map_direction_points, 2);
  lv_obj_set_style_line_width(map_direction, 2, 0);
  lv_obj_set_style_line_color(map_direction, lv_color_hex(0xffd166), 0);
  lv_obj_set_style_line_rounded(map_direction, true, 0);
  lv_obj_set_size(map_direction, lv_pct(100), lv_pct(100));
  lv_obj_set_pos(map_direction, 0, 0);

  lv_obj_t *map_north_label = lv_label_create(map_square);
  lv_label_set_text(map_north_label, "N");
  lv_obj_set_style_text_color(map_north_label, lv_color_hex(0xffd166), 0);

  for (int i = 0; i < PlanesService::MAX_PLANES; ++i) {
    lv_obj_t *dot = lv_obj_create(map_square);
    lv_obj_set_size(dot, 6, 6);
    lv_obj_set_style_bg_color(dot, lv_color_hex(0xfa5252), 0);
    lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(dot, 0, 0);
    lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_add_flag(dot, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(dot, LV_OBJ_FLAG_SCROLLABLE);
    app_ctx.map_plane_dots[i] = dot;
  }

  lv_obj_t *map_summary = lv_label_create(map_view);
  lv_obj_set_style_text_color(map_summary, lv_color_white(), 0);
  lv_obj_set_style_text_font(map_summary, &lv_font_montserrat_14, 0);

  lv_obj_t *map_hint = lv_label_create(map_view);
  lv_obj_set_style_text_color(map_hint, lv_color_hex(0xb7bdd7), 0);
  lv_label_set_long_mode(map_hint, LV_LABEL_LONG_WRAP);

  app_ctx.screen = scr;
  app_ctx.content = content;
  app_ctx.list_view = list_view;
  app_ctx.map_view = map_view;
  app_ctx.map_toggle_btn = map_btn;
  app_ctx.map_toggle_label = map_lbl;
  app_ctx.status_label = status_label;
  app_ctx.count_label = count_label;
  app_ctx.map_hint_label = map_hint;
  app_ctx.map_summary_label = map_summary;
  app_ctx.map_square = map_square;
  app_ctx.map_bg = map_bg;
  app_ctx.map_circle = map_circle;
  app_ctx.map_center_dot = map_center_dot;
  app_ctx.map_direction = map_direction;
  app_ctx.map_north_label = map_north_label;
  app_ctx.refresh_timer = lv_timer_create(planes_app_timer_cb, 3000, nullptr);
  app_ctx.map_mode = false;
  planes_app_apply_view_mode();

  PlanesService::instance().resume();
  planes_app_update_ui();
  ui_load_screen(scr, false);
  UI_TRACE("Planes", "launch loaded screen");
  
  // Set route after screen is loaded
  UiSessionState::set_route("planes");
}
