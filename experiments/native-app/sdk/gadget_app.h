#ifndef GADGET_APP_H
#define GADGET_APP_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#define GADGET_APP_API_MAJOR 1
#define GADGET_APP_API_MINOR 0
/* All calls and callbacks run synchronously on the UI thread. Handles expire
 * at shutdown. No tasks, interrupts, exceptions, or background callbacks. */
typedef void *gadget_handle;
typedef void (*gadget_callback)(void *user);
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
