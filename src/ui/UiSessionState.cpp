#include "ui/UiSessionState.h"

#include "services/ConfigStoreService.h"
#include "ui/UiTrace.h"

#include <lvgl.h>

namespace UiSessionState {
namespace {

struct SessionState {
  bool initialized = false;
  std::string current = "home";
  std::string persisted = "home";
  lv_timer_t *save_timer = nullptr;
};

static SessionState state;

static void save_now()
{
  if (!state.initialized) return;
  if (state.current == state.persisted) return;

  UI_TRACE("Route", "save_now begin current=%s persisted=%s",
           state.current.c_str(),
           state.persisted.c_str());

  ConfigStoreService &cfg = ConfigStoreService::instance();
  if (!cfg.load("ui_state")) {
    UI_TRACE("Route", "save_now cfg.load(ui_state) failed");
    return;
  }

  cfg.set("route", state.current);
  if (cfg.save("ui_state")) {
    state.persisted = state.current;
    UI_TRACE("Route", "save_now success route=%s", state.persisted.c_str());
  } else {
    UI_TRACE("Route", "save_now cfg.save(ui_state) failed route=%s", state.current.c_str());
  }
}

static void save_timer_cb(lv_timer_t *timer)
{
  (void)timer;
  save_now();
}

static void schedule_save()
{
  if (state.save_timer) {
    UI_TRACE("Route", "schedule_save reset existing timer");
    lv_timer_reset(state.save_timer);
    return;
  }

  state.save_timer = lv_timer_create(save_timer_cb, 3000, nullptr);
  if (state.save_timer) {
    lv_timer_set_repeat_count(state.save_timer, 1);
    UI_TRACE("Route", "schedule_save created timer delay_ms=3000");
  } else {
    UI_TRACE("Route", "schedule_save failed to create timer");
  }
}

}  // namespace

void begin()
{
  if (state.initialized) return;

  ConfigStoreService &cfg = ConfigStoreService::instance();
  if (cfg.load("ui_state")) {
    std::string route = cfg.get("route", "home");
    if (!route.empty()) {
      state.current = route;
      state.persisted = route;
    }
  }

  UI_TRACE("Route", "begin current=%s persisted=%s",
           state.current.c_str(),
           state.persisted.c_str());

  state.initialized = true;
}

void set_route(const char *route)
{
  if (!state.initialized || !route || route[0] == '\0') return;

  if (state.current == route) return;
  UI_TRACE("Route", "set_route from=%s to=%s", state.current.c_str(), route);
  state.current = route;
  schedule_save();
}

const std::string &current_route()
{
  return state.current;
}

}  // namespace UiSessionState
