#pragma once
#include "gadget_app.h"
#include "experimental/NativePackage.h"
#include <atomic>
#include <cstring>
#include <cstdlib>
#ifdef ARDUINO
#include <esp_heap_caps.h>
#endif
namespace native_app {
constexpr uint32_t max_http_body=2048;
/* UI-thread ownership. Worker results carry only a generation, never code or
 * user pointers. Clear before unloading. Taking also clears before re-entry. */
struct HttpDelivery {
    uint32_t generation=0;
    gadget_http_callback callback=nullptr;
    void *user=nullptr;
    void clear() { callback=nullptr; user=nullptr; }
    bool deliver(uint32_t token, DeliveryGate &gate, int32_t status,
                 const char *body, uint32_t size) {
        if (!callback || generation!=token || !gate.enter()) return false;
        auto cb=callback; auto u=user; clear();
        cb(status,body,size,u); gate.leave(); return true;
    }
};
int32_t json_number_scaled(const char *,uint32_t,const char *,int32_t,int32_t *);
#if GADGET_OS_NATIVE_APP_LOADER
namespace http {
struct Result {
    std::atomic<bool> done{false}, cancelled{false};
    uint32_t generation=0, size=0, elapsed_ms=0;
    int32_t status=GADGET_HTTP_TRANSPORT;
    char url[256]{};char *body=nullptr;uint32_t capacity=max_http_body,retry_seconds=0;
    explicit Result(uint32_t limit=max_http_body):capacity(limit){
#ifdef ARDUINO
        body=(char*)heap_caps_calloc(limit+1,1,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
#else
        body=(char*)calloc(limit+1,1);
#endif
    }
    ~Result(){free(body);}
    Result(const Result&)=delete;Result&operator=(const Result&)=delete;
    bool append(const uint8_t *bytes,size_t count) {
        if (cancelled.load()) return false;
        if (!body || count>capacity-size) {status=GADGET_HTTP_TOO_LARGE; return false;}
        memcpy(body+size,bytes,count); size+=count; body[size]=0; return true;
    }
};
int32_t start(const char *url,uint32_t generation,uint32_t capacity=max_http_body);
void cancel(uint32_t generation);
Result *take_completed(); // UI owns/deletes result; worker no longer accesses it.
}
#endif
}
