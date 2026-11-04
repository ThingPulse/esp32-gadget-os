#include "experimental/NativeHttp.h"
#include <cassert>
#include <cstring>
#include <string>
#include <fstream>
#include <iterator>
#include <iostream>
#include <dlfcn.h>
using namespace native_app;
static DeliveryGate gate;
static HttpDelivery delivery;
static uint32_t generation;
static int transport_result;
static gadget_callback refresh_cb,close_cb,tick_cb;
static std::string labels[3];
static int label_count;
static void log_message(const char *) {}
static gadget_handle screen() {return labels;}
static gadget_handle label(gadget_handle,const char *text) {assert(label_count<3); auto *p=&labels[label_count++]; *p=text; return p;}
static gadget_handle button(gadget_handle,const char *text,gadget_callback cb,void *) {
    if(!strcmp(text,"Close")) close_cb=cb; else refresh_cb=cb; return labels;
}
static int set_label(gadget_handle h,const char *text) {assert(!gate.closing); *static_cast<std::string*>(h)=text; return 0;}
static gadget_handle timer(uint32_t,gadget_callback cb,void *) {tick_cb=cb; return labels;}
static void close_app() {gate.close(); delivery.clear();}
static int32_t http_get(const char *url,gadget_http_callback cb,void *user) {
    assert(std::string(url)=="https://api.wheretheiss.at/v1/satellites/25544");
    if(!transport_result) delivery={generation,cb,user}; return transport_result;
}
static void call(gadget_callback cb) {if(gate.enter()) {cb(nullptr); gate.leave();}}
static const char good[]={"{\"latitude\":-47.1234,\"longitude\":8.0567,\"altitude\":420.16,\"velocity\":27600.6}"};
static bool complete(uint32_t token,int32_t status,const char *body) {return delivery.deliver(token,gate,status,body,strlen(body));}
int main(int argc,char **argv)
{
    assert(argc==4);
    http::Result buffer;
    std::string maximum(max_http_body,'x');
    assert(buffer.append((const uint8_t*)maximum.data(),maximum.size()));
    assert(buffer.size==max_http_body && buffer.body[max_http_body]==0);
    assert(!buffer.append((const uint8_t*)"x",1) && buffer.status==GADGET_HTTP_TOO_LARGE);
    buffer.size=0; buffer.cancelled.store(true);
    assert(!buffer.append((const uint8_t*)"x",1) && buffer.size==0);
    gate={}; delivery={1,[](int32_t,const char*,uint32_t,void*) {
        assert(!delivery.callback); close_app(); assert(!gate.can_unload());
    },nullptr};
    assert(complete(1,200,"{}") && gate.can_unload());
    for(int i=2;i<4;++i) {std::ifstream f(argv[i],std::ios::binary); std::string bytes{std::istreambuf_iterator<char>(f),{}};
        std::string error; assert(!bytes.empty()); assert(validate_elf((const uint8_t*)bytes.data(),bytes.size(),error));}
    int32_t value=123;
    assert(json_number_scaled(good,strlen(good),"latitude",1000,&value)==0 && value==-47123);
    for(const char *bad:{"{}","[]","{","{\"x\":true}","{\"x\":\"1\"}","{\"x\":null}","{\"x\":1e100}"})
        assert(json_number_scaled(bad,strlen(bad),"x",1,&value)!=0);
    assert(json_number_scaled(good,max_http_body+1,"latitude",1,&value)!=0);
    assert(json_number_scaled(good,strlen(good),"latitude",0,&value)!=0);
    assert(json_number_scaled(good,strlen(good),"latitude",1000001,&value)!=0);
    assert(json_number_scaled(good,strlen(good),"latitude",1,nullptr)!=0);
    for(unsigned i=0;i<100;++i) {
        gate={}; delivery={}; label_count=0; ++generation; transport_result=0;
        auto *lib=dlopen(argv[1],RTLD_NOW|RTLD_LOCAL); assert(lib);
        auto entry=(int(*)(int,char**))dlsym(lib,"gadget_app_entry"); assert(entry);
        gadget_host_api host={sizeof(host),1,1,log_message,screen,label,button,set_label,timer,close_app,http_get,json_number_scaled};
        gadget_app_lifecycle app={sizeof(app),1,1,nullptr}; char *args[]={(char*)&host,(char*)&app};
        host.minor=0; assert(entry(2,args)!=0); host.minor=1;
        host.struct_size=offsetof(gadget_host_api,http_get_json); assert(entry(2,args)!=0); host.struct_size=sizeof(host);
        assert(entry(2,args)==0 && app.shutdown && delivery.callback);
        assert(!complete(generation-1,200,good)); // Old app cannot receive this response.
        assert(complete(generation,200,good)); assert(!delivery.callback);
        assert(labels[1]=="Latitude: -47.123\nLongitude: 8.057\nAltitude: 420.2 km\nSpeed: 27601 km/h");
        for(int j=0;j<5;++j) call(tick_cb);
        call(refresh_cb); assert(delivery.callback); assert(complete(generation,200,"{\"latitude\":900}"));
        assert(labels[2].find("Invalid response")!=std::string::npos);
        for(int j=0;j<5;++j) call(tick_cb);
        transport_result=GADGET_HTTP_OFFLINE; call(refresh_cb); assert(!delivery.callback);
        assert(labels[2].find("No Wi-Fi")!=std::string::npos);
        for(int j=0;j<5;++j) call(tick_cb);
        transport_result=0; call(refresh_cb); assert(delivery.callback);
        assert(complete(generation,429,"")); assert(labels[2].find("Service busy")!=std::string::npos);
        call(refresh_cb); assert(!delivery.callback); // Rate-limit delay is app-owned.
        for(int j=0;j<60;++j) call(tick_cb);
        call(refresh_cb); assert(delivery.callback);
        call(close_cb); assert(gate.can_unload() && !delivery.callback);
        app.shutdown(); assert(dlclose(lib)==0);
        // Completion after actual dlclose must not execute the freed callback.
        assert(!complete(generation,200,good));
        // A reopened session must not receive its predecessor's result either.
        gate={}; label_count=0; ++generation;
        lib=dlopen(argv[1],RTLD_NOW|RTLD_LOCAL); assert(lib);
        entry=(int(*)(int,char**))dlsym(lib,"gadget_app_entry"); assert(entry(2,args)==0);
        assert(!complete(generation-1,200,good)); assert(delivery.callback);
        gate.close(); assert(!complete(generation,200,good)); // Queued completion after close intent.
        delivery.clear(); app.shutdown(); assert(dlclose(lib)==0);
    }
    std::cout<<"PASS: ISS ELF validation, bounded JSON, API compatibility, success/offline/invalid/rate-limit UI, 100 close-pending/unload/reopen/late-result cycles\n";
}
