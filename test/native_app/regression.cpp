#include "experimental/NativePackage.h"
#include "gadget_app.h"
#include <cassert>
#include <cstring>
#include <fstream>
#include <iterator>
#include <iostream>
#include <dlfcn.h>
using namespace native_app;
static std::string read(const char *p) { std::ifstream f(p,std::ios::binary); return {(std::istreambuf_iterator<char>(f)),{}}; }
static std::string manifest(const char *chip="esp32",unsigned major=1,unsigned minor=0) {
 return std::string("{\"id\":\"native-demo\",\"version\":\"0.1.0\",\"chip\":\"")+chip+"\",\"api_major\":"+std::to_string(major)+",\"api_minor\":"+std::to_string(minor)+",\"executable\":\"app.elf\"}";
}
static DeliveryGate gate;
static gadget_callback timer_cb,reset_cb,close_cb;
static int screen_handle,label_handle,button_handle,timer_handle;
static std::string label;
static void log_message(const char *) {}
static gadget_handle screen() { return &screen_handle; }
static gadget_handle make_label(gadget_handle,const char *s) { label=s; return &label_handle; }
static gadget_handle button(gadget_handle,const char *s,gadget_callback cb,void *) {
 if (!strcmp(s,"Close")) close_cb=cb; else reset_cb=cb; return &button_handle;
}
static int set_label(gadget_handle,const char *s) { label=s; return 0; }
static gadget_handle timer(uint32_t,gadget_callback cb,void *) { timer_cb=cb; return &timer_handle; }
static void close_app() { gate.close(); assert(!gate.can_unload()); }
static void call(gadget_callback cb) { if (gate.enter()) { cb(nullptr); gate.leave(); } }
int main(int argc,char **argv) {
 assert(argc==14); std::string e; Manifest m;
 assert(parse_manifest(manifest(),"esp32",m,e));
 assert(parse_manifest(manifest("esp32",1,7),"esp32",m,e));
 assert(!m.settings);
 auto settings_manifest=manifest("esp32",1,8);settings_manifest.pop_back();settings_manifest+=",\"settings\":true}";
 assert(parse_manifest(settings_manifest,"esp32",m,e)&&m.settings);
 auto wrong_settings=manifest();wrong_settings.pop_back();wrong_settings+=",\"settings\":true}";
 assert(!parse_manifest(wrong_settings,"esp32",m,e));
 wrong_settings=manifest("esp32",1,8);wrong_settings.pop_back();wrong_settings+=",\"settings\":\"true\"}";
 assert(!parse_manifest(wrong_settings,"esp32",m,e));
 assert(parse_manifest(manifest(),"esp32",m,e)&&!m.settings);

 for(int i=4;i<argc;++i){auto bytes=read(argv[i]);Package package;assert(validate_package(reinterpret_cast<const uint8_t *>(bytes.data()),bytes.size(),i%2==0?"esp32":"esp32s3",package,e));assert(package.manifest.settings);}
 auto iss=manifest("esp32",1,1); iss.replace(iss.find("native-demo"),11,"native-iss");
 assert(parse_manifest(iss,"esp32",m,e) && m.id=="native-iss");
 for (auto s:{std::string(""),std::string("{"),std::string("[]"),std::string("{}"),manifest("esp32s3"),manifest("esp32",2),manifest("esp32",1,11),std::string(2049,' ')})
  assert(!parse_manifest(s,"esp32",m,e));
 auto path=manifest(); path.replace(path.find("app.elf"),7,"../app.elf"); assert(!parse_manifest(path,"esp32",m,e));
 for (int target=1;target<=2;++target) {
  auto original=read(argv[target]); assert(!original.empty());
  assert(validate_elf((const uint8_t*)original.data(),original.size(),e));
  for (size_t n=0;n<original.size();++n) assert(!validate_elf((const uint8_t*)original.data(),n,e));
  auto damaged=original; damaged[18]=0; assert(!validate_elf((const uint8_t*)damaged.data(),damaged.size(),e));
  damaged=original; memset(&damaged[32],255,4); assert(!validate_elf((const uint8_t*)damaged.data(),damaged.size(),e));
  auto word=[](const std::string &data,size_t off) { uint32_t value; memcpy(&value,data.data()+off,4); return value; };
  const auto table=word(original,32);
  // The generated package puts .data in section 7. Removing SHF_WRITE must
  // reject before the upstream loader silently skips that section.
  damaged=original; damaged[table+7*40+8]=2;
  assert(!validate_elf((const uint8_t*)damaged.data(),damaged.size(),e));
  // Duplicate relocation destinations would otherwise relocate a pointer twice.
  const auto rela=word(original,table+6*40+16);
  damaged=original; memcpy(&damaged[rela+12],&damaged[rela],4);
  assert(!validate_elf((const uint8_t*)damaged.data(),damaged.size(),e));
  // Deterministic mutation fuzzing: acceptance isn't required, memory safety is.
  uint32_t rng=1;
  for (unsigned i=0;i<20000;++i) {
   damaged=original; rng=rng*1664525+1013904223; size_t pos=rng%damaged.size();
   rng=rng*1664525+1013904223; damaged[pos]=char(rng>>24);
   validate_elf((const uint8_t*)damaged.data(),damaged.size(),e);
  }
 }
 gate={}; assert(gate.enter()); assert(gate.enter()); gate.close(); assert(!gate.enter());
 gate.leave(); assert(!gate.can_unload()); gate.leave(); assert(gate.can_unload());
 // Exercise the actual demo logic and ABI using the host dynamic linker. This
 // deliberately does NOT claim to test Xtensa relocation or LVGL ownership.
 for (unsigned i=0;i<100;++i) {
  gate={}; auto *lib=dlopen(argv[3],RTLD_NOW|RTLD_LOCAL); assert(lib);
  auto entry=(int(*)(int,char**))dlsym(lib,"gadget_app_entry"); assert(entry);
  gadget_host_api host={offsetof(gadget_host_api,http_get_json),1,0,log_message,screen,make_label,button,set_label,timer,close_app};
  gadget_app_lifecycle app={sizeof(app),1,0,nullptr};
  char *args[]={(char*)&host,(char*)&app};
  host.major=2; assert(entry(2,args)!=0); host.major=1;
  assert(entry(2,args)==0 && app.shutdown);
  for (unsigned j=0;j<5;++j) call(timer_cb);
  assert(label=="Counter: 5"); call(reset_cb); assert(label=="Counter: 0");
  call(close_cb); assert(gate.can_unload()); call(timer_cb); assert(label=="Counter: 0");
  app.shutdown(); timer_cb=reset_cb=close_cb=nullptr; assert(dlclose(lib)==0);
 }
 std::cout<<"PASS: manifest/ELF validation, 40,000 mutations, nested closure gate, 100 desktop demo ABI cycles\n";
}
