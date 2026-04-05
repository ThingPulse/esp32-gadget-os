#pragma once

#include <string>

namespace UiSessionState {

// Loads the persisted route once at startup.
void begin();

// Schedules a debounced save if the route changed.
void set_route(const char *route);

// Returns the last known route (persisted or updated in-memory route).
const std::string &current_route();

}  // namespace UiSessionState
