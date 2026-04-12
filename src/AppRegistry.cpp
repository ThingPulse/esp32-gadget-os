#include "AppRegistry.h"

#include <cstring>

namespace {

int app_compare(const App &a, const App &b)
{
  if (a.rank != b.rank)
    return (a.rank < b.rank) ? -1 : 1;

  const char *an = a.name ? a.name : "";
  const char *bn = b.name ? b.name : "";
  return std::strcmp(an, bn);
}

}

AppRegistry &AppRegistry::instance()
{
  static AppRegistry inst;
  return inst;
}

bool AppRegistry::registerApp(const App &app)
{
  if (_count >= MAX_APPS) return false;

  _apps[_count] = app;
  ++_count;

  for (int i = _count - 1; i > 0; --i) {
    if (app_compare(_apps[i], _apps[i - 1]) >= 0)
      break;

    App tmp = _apps[i - 1];
    _apps[i - 1] = _apps[i];
    _apps[i] = tmp;
  }

  return true;
}
