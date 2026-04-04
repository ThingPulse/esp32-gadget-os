#include "AppRegistry.h"

AppRegistry &AppRegistry::instance()
{
  static AppRegistry inst;
  return inst;
}

bool AppRegistry::registerApp(const App &app)
{
  if (_count >= MAX_APPS) return false;
  _apps[_count++] = app;
  return true;
}
