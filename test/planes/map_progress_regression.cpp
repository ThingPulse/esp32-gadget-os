// Exercise the actual tile worker and observe animation positions in LVGL flushes.
#include <cassert>
#include <set>
#include "../../src/apps/PlanesApp.cpp"
static std::set<int> positions;
static uint32_t ticks() {
  return (uint32_t)std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now().time_since_epoch()).count();
}
int main() {
  lv_init();
  lv_tick_set_cb(ticks);
  auto *display = lv_display_create(320, 480);
  static uint8_t buffer[320 * 40 * 4];
  lv_display_set_buffers(display, buffer, nullptr, sizeof(buffer), LV_DISPLAY_RENDER_MODE_PARTIAL);
  lv_display_set_flush_cb(display, [](lv_display_t *d, const lv_area_t *, uint8_t *) {
    if (app_ctx.map_tiles_loading && app_ctx.loading_line &&
        !lv_obj_has_flag(app_ctx.loading_line, LV_OBJ_FLAG_HIDDEN))
      positions.insert(lv_obj_get_x(app_ctx.loading_line));
    lv_display_flush_ready(d);
  });
  app_ctx.loading_line = lv_obj_create(lv_screen_active());
  lv_obj_set_size(app_ctx.loading_line, 76, 3);
  lv_anim_t anim;
  lv_anim_init(&anim);
  lv_anim_set_var(&anim, app_ctx.loading_line);
  lv_anim_set_exec_cb(&anim, [](void *obj, int32_t x) { lv_obj_set_x((lv_obj_t *)obj, x); });
  lv_anim_set_values(&anim, -76, 320);
  lv_anim_set_duration(&anim, 900);
  lv_anim_set_repeat_count(&anim, LV_ANIM_REPEAT_INFINITE);
  lv_anim_start(&anim);
  assert(FileSystemService::instance().begin());
  auto &svc = PlanesService::instance();
  std::vector<uint16_t> pixels;
  std::string sig;
  assert(load_map_with_progress(svc, 320, pixels, sig));
  assert(pixels.size() == 320 * 320);
  assert(positions.size() > 3);
  assert(!app_ctx.map_tiles_loading);
  assert(lv_obj_has_flag(app_ctx.loading_line, LV_OBJ_FLAG_HIDDEN));
  printf("Success: spinner rendered at %zu positions during download\n", positions.size());
  FileSystemService::instance().fs().remove(PLANES_MAP_CACHE_NATIVE_META);
  setenv("MAP_TEST_FAIL", "1", 1);
  positions.clear();
  assert(!load_map_with_progress(svc, 320, pixels, sig));
  assert(positions.size() > 3);
  assert(!app_ctx.map_tiles_loading);
  assert(lv_obj_has_flag(app_ctx.loading_line, LV_OBJ_FLAG_HIDDEN));
  app_ctx.map_change_pending = true;
  planes_app_update_loading();
  assert(!lv_obj_has_flag(app_ctx.loading_line, LV_OBJ_FLAG_HIDDEN));
  app_ctx.map_change_pending = false;
  planes_app_update_loading();
  assert(lv_obj_has_flag(app_ctx.loading_line, LV_OBJ_FLAG_HIDDEN));
  puts("Failure and queued-navigation indicator checks passed");
}
