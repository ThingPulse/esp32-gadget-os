#pragma once
#if GADGET_OS_NATIVE_APP_LOADER
namespace native_app {
void launch();
void launch_iss();
void launch_id(const char *id);
bool running(const char *id);
void poll(); // after lv_task_handler returns, UI thread only
bool busy();
bool trace_enabled();
}
// Implemented by the launcher, only linked in experimental builds.
bool native_launcher_is_active();
void native_launcher_release();
void native_launcher_restore();
#endif
