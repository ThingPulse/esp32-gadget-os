#include "experimental/NativePackage.h"
#include <fstream>
#include <iterator>
#include <cassert>
#include <iostream>
int main(int argc,char**argv){assert(argc==3);for(int i=1;i<3;++i){std::ifstream f(argv[i],std::ios::binary);std::string b((std::istreambuf_iterator<char>(f)),{}),error;native_app::Package p;if(!native_app::validate_package((const uint8_t*)b.data(),b.size(),i==1?"esp32":"esp32s3",p,error)){std::cerr<<error<<"\n";return 1;}assert(p.manifest.id=="iss-tracker");}std::cout<<"PASS: both standalone ISS packages pass firmware manifest/ELF validation\n";}
