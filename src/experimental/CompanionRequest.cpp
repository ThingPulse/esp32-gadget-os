#include "experimental/CompanionRequest.h"
#include <algorithm>
#include <cctype>
#include <cstdlib>
namespace native_app { namespace companion {
int parse_request(const std::string &headers,const std::string &host,Request &out){
    auto end=headers.find("\r\n\r\n");if(end==std::string::npos)return headers.size()>2048?-431:0;
    if(end>2048)return -431;
    auto first=headers.find("\r\n");auto line=headers.substr(0,first);auto a=line.find(' '),b=line.find(' ',a+1);
    if(a==std::string::npos||b==std::string::npos||line.substr(b+1)!="HTTP/1.1")return -400;
    Request r;r.method=line.substr(0,a);r.route=line.substr(a+1,b-a-1);r.body_offset=end+4;
    if(r.method!="GET"&&r.method!="POST")return -405;
    bool length_seen=false,key_seen=false,host_seen=false;
    for(size_t p=first+2;p<end;){auto finish=headers.find("\r\n",p),colon=headers.find(':',p);if(colon>=finish)return -400;
        auto name=headers.substr(p,colon-p),v=headers.substr(colon+1,finish-colon-1);
        if(name.empty()||std::any_of(name.begin(),name.end(),[](unsigned char c){return !(std::isalnum(c)||c=='-');}))return -400;
        if(std::any_of(v.begin(),v.end(),[](unsigned char c){return (c<32&&c!='\t')||c==127;}))return -400;
        std::transform(name.begin(),name.end(),name.begin(),[](unsigned char c){return std::tolower(c);});
        while(!v.empty()&&(v.front()==' '||v.front()=='\t'))v.erase(0,1);
        if(name=="origin"||name=="transfer-encoding")return -403;
        if(name=="host"){if(host_seen||v!=host)return -403;host_seen=true;}
        if(name=="x-companion-key"){if(key_seen)return -400;r.key=v;key_seen=true;}
        if(name=="content-length"){if(length_seen||v.empty()||v.size()>4||v.find_first_not_of("0123456789")!=std::string::npos)return -400;r.length=strtoul(v.c_str(),nullptr,10);length_seen=true;}
        p=finish+2;
    }
    if(!host_seen||r.length>2048||(r.method=="POST"&&!length_seen)||(r.method=="GET"&&r.length))return -400;
    out=std::move(r);return 1;
}
}}
