#include "experimental/NativePackage.h"
#include <cassert>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
static std::string read(const char *path){std::ifstream f(path,std::ios::binary);return {std::istreambuf_iterator<char>(f),{}};}
int main(int argc,char **argv){
    assert(argc>=4);auto pem=read(argv[1]);
    for(int i=2;i<argc;++i){auto bytes=read(argv[i]);std::string path=argv[i],error;native_app::Package p;
        bool expected=path.substr(path.rfind('/')+1).rfind("ok-",0)==0;
        bool got=native_app::validate_package((const uint8_t*)bytes.data(),bytes.size(),"esp32s3",p,error,pem.c_str());
        if(got!=expected){std::cerr<<path<<": "<<error<<"\n";return 1;}
        if(!expected)continue;
        assert(p.trusted);
        assert(!native_app::validate_package((const uint8_t*)bytes.data(),bytes.size(),"esp32s3",p,error));
        assert(!native_app::validate_package((const uint8_t*)bytes.data(),bytes.size(),"esp32",p,error,pem.c_str()));
        for(size_t n=0;n<bytes.size();++n){
            assert(!native_app::validate_package((const uint8_t*)bytes.data(),n,"esp32s3",p,error,pem.c_str()));
            bytes[n]^=1;assert(!native_app::validate_package((const uint8_t*)bytes.data(),bytes.size(),"esp32s3",p,error,pem.c_str()));bytes[n]^=1;
        }
    }
    std::cout<<"PASS: authenticated package, missing/wrong key, tampering, truncation, chip and signed invalid ELF\n";
}
