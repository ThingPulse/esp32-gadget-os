// Compile together with the real TlsMemory.cpp using stubs for ESP-IDF.
#include "services/TlsMemory.h"
#include "esp_heap_caps.h"
#include <cassert>
#include <cstdlib>
#include <limits>
#include <vector>

static bool psram_present = false;
static bool fail_external = false;
static bool fail_internal = false;
static std::vector<unsigned> requests;
static void *(*allocate)(size_t, size_t) = nullptr;
static void (*release)(void *) = nullptr;
bool psramFound() { return psram_present; }
size_t heap_caps_get_free_size(unsigned) { return 100000; }
void *heap_caps_calloc(size_t count, size_t size, unsigned caps)
{
    requests.push_back(caps);
    if ((caps & MALLOC_CAP_SPIRAM) ? fail_external : fail_internal) return nullptr;
    return std::calloc(count, size);
}
void heap_caps_free(void *memory) { std::free(memory); }
int mbedtls_platform_set_calloc_free(void *(*a)(size_t, size_t), void (*f)(void *))
{
    allocate = a;
    release = f;
    return 0;
}
int main()
{
    configure_tls_memory();
    assert(!allocate); // Missing PSRAM leaves the framework allocator alone.
    psram_present = true;
    configure_tls_memory();
    assert(allocate && release);
    auto *memory = static_cast<unsigned char *>(allocate(2, 16384));
    assert(memory && requests.size() == 1 && (requests[0] & MALLOC_CAP_SPIRAM));
    for (int i = 0; i < 32768; ++i) assert(memory[i] == 0);
    release(memory);
    requests.clear();
    fail_external = true;
    memory = static_cast<unsigned char *>(allocate(1, 32));
    assert(memory && requests.size() == 2 && (requests[1] & MALLOC_CAP_INTERNAL));
    release(memory);
    fail_internal = true;
    assert(!allocate(1, 32));
    requests.clear();
    assert(!allocate(std::numeric_limits<size_t>::max(), 2));
    assert(requests.empty()); // Overflow must not produce an undersized buffer.
}
