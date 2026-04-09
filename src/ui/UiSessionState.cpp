#include "ui/UiSessionState.h"

#include "ui/UiTrace.h"

namespace UiSessionState {
namespace {

struct SessionState {
  bool initialized = false;
  std::string current = "home";
};

static SessionState state;

}  // namespace

void begin()
{
  if (state.initialized) return;

  // Temporary safety switch: disable persisted route state and timer callbacks.
  state.current = "home";

  UI_TRACE("Route", "begin persistence disabled current=%s", state.current.c_str());

  state.initialized = true;
}

void set_route(const char *route)
{
  if (!state.initialized || !route || route[0] == '\0') return;

  if (state.current == route) return;
  UI_TRACE("Route", "set_route from=%s to=%s", state.current.c_str(), route);
  state.current = route;
}

const std::string &current_route()
{
  return state.current;
}

}  // namespace UiSessionState
