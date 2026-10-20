#include "services/TlsMemory.h"
#include "ui/UiTrace.h"

#if defined(ARDUINO) && defined(ESP_PLATFORM) && defined(BOARD_HAS_PSRAM)
#include <Arduino.h>
#include <esp_heap_caps.h>
#include <mbedtls/platform.h>
#include <limits>

namespace {
void *tls_calloc(size_t count, size_t size)
{
    if (size != 0 && count > std::numeric_limits<size_t>::max() / size)
        return nullptr;
    // Arduino's precompiled mbedTLS defaults to internal RAM only. Prefer
    // external RAM for TLS, as ESP-IDF's external-memory allocator does.
    void *memory = heap_caps_calloc(count, size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!memory)
        memory = heap_caps_calloc(count, size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    return memory;
}
}
#endif

void configure_tls_memory()
{
#if defined(ARDUINO) && defined(ESP_PLATFORM) && defined(BOARD_HAS_PSRAM)
    if (!psramFound()) {
        UI_TRACE("TLS", "PSRAM unavailable; keeping internal allocator");
        return;
    }
    // Install before any networking starts and never switch back while TLS
    // sessions are live. heap_caps_free handles both memory regions.
    const int result = mbedtls_platform_set_calloc_free(tls_calloc, heap_caps_free);
    UI_TRACE("TLS", "PSRAM allocator result=%d external_free=%u internal_free=%u",
             result,
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT),
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
#endif
}
