#ifndef GADGET_APP_H
#define GADGET_APP_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#define GADGET_APP_API_MAJOR 1
#define GADGET_APP_API_MINOR 1
/* All calls and callbacks run synchronously on the UI thread. Handles expire
 * at shutdown. No tasks, interrupts, exceptions, or background callbacks. */
typedef void *gadget_handle;
typedef void (*gadget_callback)(void *user);
/* HTTP status is positive, host errors negative. JSON is borrowed for this
 * callback only. Delivery is on the UI thread and suppressed after closure. */
typedef void (*gadget_http_callback)(int32_t status, const char *json, uint32_t size, void *user);
enum { GADGET_HTTP_BUSY=-1, GADGET_HTTP_OFFLINE=-2, GADGET_HTTP_INVALID=-3,
       GADGET_HTTP_MEMORY=-4, GADGET_HTTP_TRANSPORT=-5, GADGET_HTTP_TOO_LARGE=-6,
       GADGET_HTTP_CANCELLED=-7, GADGET_HTTP_RATE_LIMIT=-8 };
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
