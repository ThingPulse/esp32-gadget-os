#pragma once

typedef void (*AppLaunchFn)(void);

static constexpr unsigned int APP_DEFAULT_BUTTON_COLOR = 0x339af0;

struct App {
  const char *name;
  const char *symbol;  // LVGL symbol string, e.g. LV_SYMBOL_SETTINGS
  AppLaunchFn on_launch;
  unsigned int button_color = APP_DEFAULT_BUTTON_COLOR;
};

class AppRegistry {
public:
  static AppRegistry &instance();
  bool registerApp(const App &app);
  const App *apps() const { return _apps; }
  int count() const { return _count; }

private:
  static const int MAX_APPS = 12;
  App _apps[MAX_APPS];
  int _count = 0;
};

// Place in a .cpp file to register an app:
//   APP_REGISTER("MyApp", LV_SYMBOL_..., my_launch_fn);
#define APP_REGISTER(name_, symbol_, fn_) \
  static bool _app_reg_##__LINE__ = AppRegistry::instance().registerApp({name_, symbol_, fn_, APP_DEFAULT_BUTTON_COLOR})

#define APP_REGISTER_COLOR(name_, symbol_, fn_, color_) \
  static bool _app_reg_##__LINE__ = AppRegistry::instance().registerApp({name_, symbol_, fn_, color_})
