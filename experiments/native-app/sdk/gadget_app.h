#ifndef GADGET_APP_H
#define GADGET_APP_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#define GADGET_APP_API_MAJOR 1
#define GADGET_APP_API_MINOR 10
/* All calls and callbacks run synchronously on the UI thread. Handles expire
 * at shutdown. No tasks, interrupts, exceptions, or background callbacks. */
typedef void *gadget_handle;
typedef void (*gadget_callback)(void *user);
enum { GADGET_GESTURE_LEFT=1,GADGET_GESTURE_RIGHT,GADGET_GESTURE_PULL };
typedef void (*gadget_gesture_callback)(uint32_t gesture,void *user);
/* HTTP status is positive, host errors negative. JSON is borrowed for this
 * callback only. Delivery is on the UI thread and suppressed after closure. */
typedef void (*gadget_http_callback)(int32_t status, const char *json, uint32_t size, void *user);
enum { GADGET_HTTP_BUSY=-1, GADGET_HTTP_OFFLINE=-2, GADGET_HTTP_INVALID=-3,
       GADGET_HTTP_MEMORY=-4, GADGET_HTTP_TRANSPORT=-5, GADGET_HTTP_TOO_LARGE=-6,
       GADGET_HTTP_CANCELLED=-7, GADGET_HTTP_RATE_LIMIT=-8 };
/* API 1.2: host-owned bounded UI. No LVGL types cross this boundary.
 * create(NULL, ROOT, ...) starts a fresh root layout. All other parents must be
 * handles returned by this table. At most 64 widgets, 160-byte text, depth 8.
 * box dimensions are logical pixels; flow is 0=absolute, 1=row, 2=column.
 * Setters return 0 on success, -1 for invalid/expired handles or arguments. */
enum { GADGET_UI_ROOT, GADGET_UI_CONTAINER, GADGET_UI_LABEL, GADGET_UI_BUTTON, GADGET_UI_BAR, GADGET_UI_CHART, GADGET_UI_SPINNER, GADGET_UI_TEXTAREA, GADGET_UI_KEYBOARD, GADGET_UI_QRCODE, GADGET_UI_SLIDER };
enum { GADGET_STYLE_DEFAULT, GADGET_STYLE_HEADER, GADGET_STYLE_CARD,
       GADGET_STYLE_TITLE, GADGET_STYLE_VALUE, GADGET_STYLE_MUTED,
       GADGET_STYLE_TAB, GADGET_STYLE_BACK, GADGET_STYLE_REFRESH,
       GADGET_STYLE_ACCENT, GADGET_STYLE_BLUE, GADGET_STYLE_ELLIPSIS, GADGET_STYLE_WHITE };
/* API 1.5: copied drawing commands, never app callbacks in the renderer. */
enum { GADGET_DRAW_LINE, GADGET_DRAW_CIRCLE };
typedef struct gadget_draw_command { int32_t kind,x,y,x2,y2,width; uint32_t color; } gadget_draw_command;
typedef struct gadget_environment {
 uint32_t struct_size,epoch,online,clock_ready,german,use24,has_location;
 int32_t latitude_e6,longitude_e6;
 char location[96];
} gadget_environment;
/* API 1.9: numeric operations for freestanding packages on Xtensa, where
 * the toolchain's non-PIC double helpers cannot be loaded with an app ELF. */
typedef struct gadget_math_api {
 uint32_t struct_size;
 double (*binary)(uint32_t op,double a,double b); /* 0 add, 1 sub, 2 mul, 3 div */
 int32_t (*compare)(double,double); /* -1,0,1; 2 unordered */
 double (*from_int)(int32_t);
 double (*from_uint)(uint32_t);
 int32_t (*to_int)(double);
 uint32_t (*to_uint)(double);
 /* API 1.10: sin, cos, sqrt. binary operation 4 is atan2(a,b). */
 double (*unary)(uint32_t,double);
} gadget_math_api;
/* API 1.10: copied request parameters. Method is GET or POST. Redirects are
 * disabled, TLS is verified (optional PEM CA), credentials never enter logs.
 * All strings are copied before returning. One request per running session. */
typedef struct gadget_http_request {
 uint32_t struct_size;
 const char *url, *method, *body, *bearer, *ca;
 uint32_t response_limit;
} gadget_http_request;
/* UI-thread setup endpoint; output must be NUL-terminated, <=capacity-1.
 * Return HTTP status. The host checks the session key before invoking apps. */
typedef int32_t (*gadget_setup_callback)(const char *method,const char *path,const char *body,char *output,uint32_t capacity,void *user);
typedef struct gadget_services_api {
 uint32_t struct_size;
 int32_t (*environment)(gadget_environment*);
 /* mode 0=clock, 1=local date + clock. */
 int32_t (*format_time)(uint32_t epoch,uint32_t mode,char*,uint32_t);
 uint32_t (*parse_time)(const char*);
 /* One bounded document per session. Slash-separated keys/array indexes. */
 int32_t (*json_open)(const char*,uint32_t);
 int32_t (*json_get)(const char *path,char*,uint32_t);
 int32_t (*json_count)(const char *path);
 void (*json_close)(void);
 /* One atomic private blob per app, <=16384 bytes. */
 int32_t (*cache_read)(void*,uint32_t);
 int32_t (*cache_write)(const void*,uint32_t);
 /* Closes the app to open location settings, then reopens it on settings exit. */
 void (*open_location)(void);
 /* Extended HTTPS: 48 KiB in 1.5, 64 KiB in 1.9, 192 KiB in 1.10.
  * Retry-After is available during callback. */
 int32_t (*http_get)(const char*,uint32_t,gadget_http_callback,void*);
 uint32_t (*http_retry_seconds)(void);
 int32_t (*json_number)(const char*,int32_t scale,int32_t*);
 /* API 1.9: finite JSON number; bounded locale-independent formatting.
  * decimals 0..8, capacity <=160. format_time mode 2 is a UTC quote timestamp.
  * Extended HTTPS and JSON documents now allow up to 65536 bytes. */
 int32_t (*json_real)(const char*,double*);
 int32_t (*format_number)(double,uint32_t,char*,uint32_t);
 uint32_t (*monotonic_ms)(void);
 const gadget_math_api *math;
 /* API 1.10: one private persistent JSON configuration, <=24576 bytes.
  * Reads return byte length (NUL included in capacity), or -1; never truncate.
  * Writes are atomic. This is separate from disposable app cache. */
 int32_t (*config_read)(char*,uint32_t);
 int32_t (*config_write)(const char*,uint32_t);
 int32_t (*http_request)(const gadget_http_request*,gadget_http_callback,void*);
 /* Temporary paired HTTP setup, expires after 10 idle minutes. The host copies
  * a <=16384 byte gzip HTML page, owns sockets, and clears callbacks on close. */
 int32_t (*setup_start)(const void*,uint32_t,gadget_setup_callback,void*,char*,uint32_t);
 void (*setup_stop)(void);
 uint32_t (*setup_active)(void);
} gadget_services_api;
typedef struct gadget_ui_api {
    uint32_t struct_size;
    gadget_handle (*create)(gadget_handle parent, uint32_t kind, const char *text,
                            gadget_callback callback, void *user);
    int32_t (*box)(gadget_handle, int32_t x, int32_t y, int32_t width, int32_t height);
    int32_t (*flow)(gadget_handle, uint32_t axis, uint32_t padding, uint32_t gap);
    int32_t (*style)(gadget_handle, uint32_t preset);
    int32_t (*text)(gadget_handle, const char *text);
    int32_t (*value)(gadget_handle, int32_t percentage);
    int32_t (*visible)(gadget_handle, uint32_t visible);
    int32_t (*selected)(gadget_handle, uint32_t selected);
    /* Text alignment: 0=left, 1=center, 2=right. */
    int32_t (*align)(gadget_handle, uint32_t alignment);
    /* API 1.4: fixed 0..100 line chart; up to 60 copied samples, -1 is a gap. */
    int32_t (*chart)(gadget_handle,const int32_t *values,uint32_t count);
    int32_t (*opacity)(gadget_handle,uint32_t percent);
    /* API 1.5: opt-in quota <=256; clear invalidates descendant handles. */
    int32_t (*reserve)(uint32_t count);
    int32_t (*clear)(gadget_handle);
    int32_t (*scroll)(gadget_handle,uint32_t enabled);
    int32_t (*paint)(gadget_handle,uint32_t foreground,uint32_t background,uint32_t opacity);
    int32_t (*draw)(gadget_handle,const gadget_draw_command*,uint32_t count);
    int32_t (*wrap)(gadget_handle);

    /* API 1.9: bounded text fields, shared touch keyboard. get returns byte
     * length or -1; never truncates. Config max_chars is 1..255. Clearing the
     * tree invalidates input/keyboard handles like every other widget. */
    int32_t (*input_config)(gadget_handle,uint32_t max_chars,uint32_t multiline);
    int32_t (*input_get)(gadget_handle,char*,uint32_t capacity);
    int32_t (*keyboard_attach)(gadget_handle keyboard,gadget_handle input);
    /* -1 reads, 0..4096 sets vertical scroll position. */
    int32_t (*scroll_y)(gadget_handle,int32_t);
    int32_t (*gestures)(gadget_handle,gadget_gesture_callback,void*);
    /* 1..96 normalized samples spread across the full chart width; -1 is a gap. */
    int32_t (*chart_fit)(gadget_handle,const int32_t*,uint32_t);
    /* Defer rebuilding a view while a pointer is held or its container scrolls. */
    int32_t (*interaction_active)(gadget_handle);
    /* API 1.10: QR text <=256 bytes; square 80..240. Slider callback fires
     * on release, value_get reads its current value. No LVGL types escape. */
    int32_t (*qr)(gadget_handle,const char*,uint32_t);
    int32_t (*slider)(gadget_handle,int32_t,int32_t,int32_t);
    int32_t (*value_get)(gadget_handle);
} gadget_ui_api;
enum { GADGET_LAUNCH_NORMAL, GADGET_LAUNCH_SETTINGS };
typedef struct gadget_host_api {
    uint32_t struct_size;
    uint16_t major, minor;
    void (*log)(const char *message);
    gadget_handle (*screen_create)(void);
    gadget_handle (*label_create)(gadget_handle screen, const char *text);
    gadget_handle (*button_create)(gadget_handle screen, const char *text,
                                    gadget_callback callback, void *user);
    int (*label_set_text)(gadget_handle label, const char *text);
    gadget_handle (*timer_create)(uint32_t period_ms, gadget_callback callback, void *user);
    void (*request_close)(void);
    /* API 1.1: one host-owned request, HTTPS only, response <=2048 bytes.
     * Returns 0 when accepted. Closing cancels delivery immediately; the host
     * worker may finish later. It never retains pointers into app memory. */
    int32_t (*http_get_json)(const char *url, gadget_http_callback callback, void *user);
    /* Read one top-level numeric JSON field, multiplied by scale and rounded.
     * No JSON/runtime objects or floating-point helpers cross the app boundary. */
    int32_t (*json_number_scaled)(const char *json, uint32_t size, const char *key,
                                  int32_t scale, int32_t *value);
    /* Append-only ABI: old binaries continue using the unchanged prefix. */
    const gadget_ui_api *ui;
    /* Per-app private strings: keys [a-z0-9_], max 24 chars; max 8 keys,
     * values <=255 bytes. get returns length or -1; never truncates. */
    int32_t (*setting_get)(const char *key, char *value, uint32_t capacity);
    int32_t (*setting_set)(const char *key, const char *value);
    /* API 1.3: local paired companion snapshot for Agent Usage. Returns JSON
     * length or -1 (no data / unavailable). Capacity includes NUL. RAM-only;
     * host adjusts observation ages and reset countdowns on every read. */
    int32_t (*companion_read)(char *json, uint32_t capacity);
    /* API 1.4: paired System Monitor snapshot; -2 means no computer paired. */
    int32_t (*system_read)(char *json,uint32_t capacity);
    int32_t (*system_refresh)(void);
    int32_t (*json_string)(const char *json,uint32_t size,const char *key,char *out,uint32_t capacity);
    const gadget_services_api *services; /* API 1.5 */
    /* API 1.6: independent activity snapshot; -2 means no computer paired. */
    int32_t (*activity_read)(char *json,uint32_t capacity);
    /* API 1.7: opaque JSON channel in the running app's namespace. Channel name
     * [a-z][a-z0-9-]{0,31}, payload <=2048 bytes, capacity includes NUL.
     * Returns byte length, -1 unavailable/invalid/too small, -2 unpaired.
     * Payload bytes are unchanged. age_ms is monotonic time since receipt;
     * the app owns schema validation, observation ages and expiry. RAM-only,
     * bounded cache: absence/eviction is normal. Older readers above are frozen
     * compatibility adapters, not interfaces for new payload development. */
    int32_t (*companion_channel_read)(const char *channel,char *json,
                                      uint32_t capacity,uint32_t *age_ms);
    /* API 1.8: settings=true in the manifest opts into a settings entry.
     * Build the app-owned settings view when this returns SETTINGS. Closing
     * returns to Settings > Apps. An in-app gear can call the same renderer
     * directly and handle its local Back action without unloading the app. */
    uint32_t (*launch_mode)(void);
} gadget_host_api;
typedef struct gadget_app_lifecycle {
    uint32_t struct_size;
    uint16_t major, minor;
    /* Set before allocating resources; called even when initialization fails. */
    void (*shutdown)(void);
} gadget_app_lifecycle;
/* ELF entry is deliberately the loader's argc/argv signature, but argv contains
 * two opaque pointers, NOT strings: host API and writable lifecycle descriptor.
 * Return 0 on success. Host owns both structures. No global C++ constructors. */
int gadget_app_entry(int argc, char **argv);
#ifdef __cplusplus
}
#endif
#endif
