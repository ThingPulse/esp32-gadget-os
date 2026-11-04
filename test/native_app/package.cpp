#include "experimental/NativePackage.h"
#include <cassert>
#include <fstream>
#include <iterator>
#include <iostream>
using namespace native_app;
int main(int argc,char **argv){
 assert(argc>1);
 for(int i=1;i<argc;++i){std::string path=argv[i];std::ifstream f(path,std::ios::binary);std::string b{std::istreambuf_iterator<char>(f),{}};
  bool want=path.substr(path.rfind('/')+1).rfind("ok-",0)==0;Package p;std::string e;
  bool got=validate_package((const uint8_t*)b.data(),b.size(),"esp32s3",p,e);
  if(got!=want){std::cerr<<path<<": "<<e<<"\n";return 1;}
  if(want){
   for(size_t n=0;n<b.size();++n)assert(!validate_package((const uint8_t*)b.data(),n,"esp32s3",p,e));
   for(size_t n=0;n<b.size();++n){b[n]^=1;assert(!validate_package((const uint8_t*)b.data(),b.size(),"esp32s3",p,e));b[n]^=1;}
  }
 }
 std::cout<<"PASS: package compatibility, metadata, ELF, checksum, all truncations and every single-byte corruption\n";
}
