#include "gadget_app.h"
#include <stddef.h>
static const gadget_host_api *host;
static gadget_handle label, title;
static uint32_t count;
static void update(void)
{
    char text[32];
    const char *prefix = "Counter: ";
    for (unsigned i = 0; i < 9; ++i) text[i] = prefix[i];
    char digits[10];
    unsigned n = count, len = 0, pos = 9;
    do { digits[len++] = '0' + n % 10; n /= 10; } while (n);
    while (len) text[pos++] = digits[--len];
    text[pos] = 0;
    host->label_set_text(label, text);
}
static void tick(void *unused) { (void)unused; ++count; update(); }
static void reset(void *unused)
{
    (void)unused; count = 0;
    host->label_set_text(title, "Reset received");
    update(); host->log("reset button received");
}
static void close_app(void *unused) { (void)unused; host->log("close button received"); host->request_close(); }
static void shutdown_app(void) { host->log("counter shutdown"); label = title = 0; }
__attribute__((visibility("default")))
int gadget_app_entry(int argc, char **argv)
{
    if (argc != 2 || !argv) return -1;
    host = (const gadget_host_api *)argv[0];
    gadget_app_lifecycle *app = (gadget_app_lifecycle *)argv[1];
    if (!host || host->struct_size < offsetof(gadget_host_api, http_get_json) || host->major != GADGET_APP_API_MAJOR ||
        !app || app->struct_size < sizeof(*app)) return -2;
    app->major = GADGET_APP_API_MAJOR;
    app->minor = 0; /* Counter still only needs API 1.0. */
    app->shutdown = shutdown_app;
    count = 0;
    gadget_handle screen = host->screen_create();
    if (!screen) return -3;
    title = host->label_create(screen, "External native app");
    if (!title) return -3;
    label = host->label_create(screen, "Counter: 0");
    if (!label || !host->button_create(screen, "Reset counter", reset, 0) ||
        !host->button_create(screen, "Close", close_app, 0) ||
        !host->timer_create(250, tick, 0)) return -4;
    host->log("counter initialized from filesystem ELF");
    return 0;
}
