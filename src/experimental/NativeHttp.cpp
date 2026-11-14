#if GADGET_OS_NATIVE_APP_LOADER
#include "experimental/NativeHttp.h"
#include <Arduino.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <freertos/task.h>
#include <new>
#include "services/UpdateService.h"
#include "services/DeviceCheckInPolicy.h"
#include <cstring>
namespace native_app::http {
namespace {
Result *pending=nullptr; // UI thread only; worker receives a copied pointer.
uint32_t last_start=0;
bool started=false;
class Body:public Stream {
    Result &r;
    uint32_t start;
public:
    bool oversized=false;
    Body(Result &result,uint32_t began):r(result),start(began) {}
    size_t write(uint8_t b)override {return write(&b,1);}
    size_t write(const uint8_t *b,size_t n)override {
        if (r.cancelled.load() || uint32_t(millis()-start)>20000) return 0;
        if (!r.append(b,n)) { oversized=r.status==GADGET_HTTP_TOO_LARGE; return 0; }
        return n;
    }
    int available()override{return 0;} int read()override{return -1;}
    int peek()override{return -1;} void flush()override{}
};
void request(Result &r,uint32_t began)
{
    if (r.cancelled.load()) return;
    WiFiClientSecure secure;WiFiClient plain;
    if(r.ca.empty())secure.useBuiltinCACertBundle();else secure.setCACert(r.ca.c_str());
    secure.setHandshakeTimeout(8);
    HTTPClient http;
    http.setConnectTimeout(5000); http.setTimeout(5000);
    http.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);
    if (!http.begin(r.url.compare(0,8,"https://")==0?static_cast<NetworkClient&>(secure):static_cast<NetworkClient&>(plain),r.url.c_str())) return;
    http.addHeader("Accept","application/json");
    http.addHeader("User-Agent","GadgetOS-NativeExperiment/0.2");
    Serial.printf("[NATIVE HTTP] generation=%u phase=request\n",r.generation);
    const char *headers[]={"Retry-After"};http.collectHeaders(headers,1);
    if(!r.bearer.empty())http.addHeader("Authorization",("Bearer "+r.bearer).c_str());
    if(r.method=="POST")http.addHeader("Content-Type","application/json");
    int status=r.method=="POST"?http.POST((uint8_t*)r.request_body.data(),r.request_body.size()):http.GET();
    r.retry_seconds=DeviceCheckInPolicy::retry_after_ms(http.header("Retry-After").c_str())/1000;
    if (status<=0) status=GADGET_HTTP_TRANSPORT;
    if (status>=200 && status<300 && !r.cancelled.load()) {
        Body sink(r,began);
        if (http.getSize()>int(r.capacity)) status=GADGET_HTTP_TOO_LARGE;
        else if (http.writeToStream(&sink)<0) status=sink.oversized?GADGET_HTTP_TOO_LARGE:GADGET_HTTP_TRANSPORT;
    }
    r.status=status>0?status:(status==GADGET_HTTP_TOO_LARGE?status:GADGET_HTTP_TRANSPORT);
    http.end();
}
void worker(void *arg)
{
    auto *r=static_cast<Result *>(arg);
    uint32_t began=millis();
    request(*r,began); // All transport objects destroyed before publishing.
    if (r->cancelled.load()) r->status=GADGET_HTTP_CANCELLED;
    if (r->status<200||r->status>=300) {r->size=0; r->body[0]=0;}
    r->elapsed_ms=millis()-began;
    r->done.store(true,std::memory_order_release);
    // UI may delete r immediately. No further dereference, even for logging.
    vTaskDelete(nullptr);
}
}
int32_t start(const char *url,uint32_t generation,uint32_t capacity)
{
 if(!url||strncmp(url,"https://",8)||strlen(url)>2047||capacity>196608)return GADGET_HTTP_INVALID;
 if(started&&uint32_t(millis()-last_start)<5000)return GADGET_HTTP_RATE_LIMIT;
 gadget_http_request args{sizeof(args),url,"GET","","","",capacity};return start_request(&args,generation);
}
int32_t start_request(const gadget_http_request *args,uint32_t generation)
{
 if(!args||args->struct_size<sizeof(*args)||!args->url||!args->method||!args->body||!args->bearer||!args->ca||args->response_limit<1||args->response_limit>196608)return GADGET_HTTP_INVALID;
 const std::string url=args->url,method=args->method,body=args->body,token=args->bearer,ca=args->ca;
 size_t start=url.compare(0,8,"https://")==0?8:url.compare(0,7,"http://")==0?7:0;
 if(!start||url.size()>2047||url.size()<=start||url.find_first_of("@#")!=std::string::npos||
    (method!="GET"&&method!="POST")||body.size()>24576||token.size()>4096||ca.size()>8192)return GADGET_HTTP_INVALID;
 auto end=url.find('/',start);auto authority=url.substr(start,end-start);
 if(authority.empty()||authority.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789.-:[]")!=std::string::npos)return GADGET_HTTP_INVALID;
 for(unsigned char c:url)if(c<=32||c>=127)return GADGET_HTTP_INVALID;
 if(token.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789._-=")!=std::string::npos)return GADGET_HTTP_INVALID;
 auto update=UpdateService::instance().state();
 if(update==UpdateService::State::Downloading||update==UpdateService::State::Checking||update==UpdateService::State::ReadyToReboot||pending)return GADGET_HTTP_BUSY;
 if(WiFi.status()!=WL_CONNECTED)return GADGET_HTTP_OFFLINE;
 if(started&&uint32_t(millis()-last_start)<250)return GADGET_HTTP_RATE_LIMIT;
 auto*r=new(std::nothrow)Result(args->response_limit);if(!r)return GADGET_HTTP_MEMORY;
 if(!r->body){delete r;return GADGET_HTTP_MEMORY;}
 r->url=url;r->method=method;r->request_body=body;r->bearer=token;r->ca=ca;r->generation=generation;
 pending=r;
 if(xTaskCreate(worker,"native-http",12288,r,1,nullptr)!=pdPASS){pending=nullptr;delete r;return GADGET_HTTP_MEMORY;}
 last_start=millis();started=true;return 0;
}
void cancel(uint32_t generation)
{
    if (pending && pending->generation==generation) pending->cancelled.store(true);
}
Result *take_completed()
{
    if (!pending || !pending->done.load(std::memory_order_acquire)) return nullptr;
    auto *r=pending; pending=nullptr; return r;
}
}
#endif
