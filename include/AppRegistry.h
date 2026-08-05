#pragma once

#include <lvgl.h>

typedef void (*AppLaunchFn)(void);
typedef lv_obj_t *(*AppIconCreateFn)(lv_obj_t *parent, lv_coord_t size);

static constexpr unsigned int APP_DEFAULT_BUTTON_COLOR = 0x339af0;
static constexpr int APP_DEFAULT_RANK = 50;

enum class AppAvailability {
  Stable,
  Experimental,
};

struct App {
  App() = default;
  App(const char *n, const char *s, AppLaunchFn fn, unsigned int color,
      int r = APP_DEFAULT_RANK,
      AppAvailability availability_ = AppAvailability::Stable,
      AppIconCreateFn icon_create_ = nullptr)
      : name(n), symbol(s), on_launch(fn), button_color(color), rank(r),
        availability(availability_), icon_create(icon_create_) {}

  const char *name;
  const char *symbol;  // LVGL symbol string, e.g. LV_SYMBOL_SETTINGS
  AppLaunchFn on_launch;
  unsigned int button_color = APP_DEFAULT_BUTTON_COLOR;
  int rank = APP_DEFAULT_RANK;
  AppAvailability availability = AppAvailability::Stable;
  AppIconCreateFn icon_create = nullptr;
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
  static bool _app_reg_##__LINE__ = AppRegistry::instance().registerApp(App{name_, symbol_, fn_, APP_DEFAULT_BUTTON_COLOR, APP_DEFAULT_RANK})

#define APP_REGISTER_COLOR(name_, symbol_, fn_, color_) \
  static bool _app_reg_##__LINE__ = AppRegistry::instance().registerApp(App{name_, symbol_, fn_, color_, APP_DEFAULT_RANK})

#define APP_REGISTER_RANK(name_, symbol_, fn_, rank_) \
  static bool _app_reg_##__LINE__ = AppRegistry::instance().registerApp(App{name_, symbol_, fn_, APP_DEFAULT_BUTTON_COLOR, rank_})

#define APP_REGISTER_COLOR_RANK(name_, symbol_, fn_, color_, rank_) \
  static bool _app_reg_##__LINE__ = AppRegistry::instance().registerApp(App{name_, symbol_, fn_, color_, rank_})

#define APP_REGISTER_EXPERIMENTAL_COLOR_RANK(name_, symbol_, fn_, color_, rank_) \
  static bool _app_reg_##__LINE__ = AppRegistry::instance().registerApp(           \
      App{name_, symbol_, fn_, color_, rank_, AppAvailability::Experimental})

#define APP_REGISTER_CUSTOM_ICON_COLOR_RANK(name_, icon_fn_, fn_, color_, rank_) \
  static bool _app_reg_##__LINE__ = AppRegistry::instance().registerApp(           \
      App{name_, nullptr, fn_, color_, rank_, AppAvailability::Stable, icon_fn_})
