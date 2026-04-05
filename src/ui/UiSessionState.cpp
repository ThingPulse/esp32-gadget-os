#include "ui/UiSessionState.h"

#include "services/ConfigStoreService.h"

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

  ConfigStoreService &cfg = ConfigStoreService::instance();
  if (!cfg.load("ui_state")) return;

  cfg.set("route", state.current);
  if (cfg.save("ui_state"))
    state.persisted = state.current;
}

static void save_timer_cb(lv_timer_t *timer)
{
  (void)timer;
  save_now();
}

static void schedule_save()
{
  if (state.save_timer) {
    lv_timer_reset(state.save_timer);
    return;
  }

  state.save_timer = lv_timer_create(save_timer_cb, 3000, nullptr);
  if (state.save_timer)
    lv_timer_set_repeat_count(state.save_timer, 1);
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

  state.initialized = true;
}

void set_route(const char *route)
{
  if (!state.initialized || !route || route[0] == '\0') return;

  if (state.current == route) return;
  state.current = route;
  schedule_save();
}

const std::string &current_route()
{
  return state.current;
}

}  // namespace UiSessionState
