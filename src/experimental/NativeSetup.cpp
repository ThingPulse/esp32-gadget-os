#if GADGET_OS_NATIVE_APP_LOADER || !defined(ARDUINO)
#include "experimental/NativeSetup.h"
#include <lvgl.h>
#include <cstring>
#include <cstdio>
#include <random>
#include <algorithm>
#include <cctype>
#include <cstdlib>

#if defined(ARDUINO)
#include <WiFi.h>
#include <esp_system.h>
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <fcntl.h>
#include <cerrno>
#endif
namespace native_app::setup {
bool server_start();
namespace {
std::string input,output,page,key;gadget_setup_callback callback=nullptr;void*user=nullptr;uint32_t expires=0;size_t sent=0;uint32_t deadline=0;bool active=false;
#if defined(ARDUINO)
WiFiServer server(8080);WiFiClient peer;
bool peer_open(){return bool(peer);}
void drop(){peer.stop();input.clear();output.clear();sent=0;}
#else
int server=-1,peer=-1;
bool peer_open(){return peer>=0;}
void drop(){if(peer>=0)close(peer);peer=-1;input.clear();output.clear();sent=0;}
#endif
void respond(int code,const std::string&body,const char*type,bool gzip=false){output="HTTP/1.1 "+std::to_string(code)+" OK\r\nContent-Type: "+type+"\r\nContent-Length: "+std::to_string(body.size())+"\r\nCache-Control: no-store\r\nX-Content-Type-Options: nosniff\r\nReferrer-Policy: no-referrer\r\nContent-Security-Policy: default-src 'self'; script-src 'unsafe-inline'; style-src 'unsafe-inline'; connect-src 'self'; frame-ancestors 'none'; base-uri 'none'\r\nConnection: close\r\n\r\n"+body;if(gzip)output.insert(output.find("\r\n")+2,"Content-Encoding: gzip\r\n");sent=0;}
void parse(){
 auto end=input.find("\r\n\r\n");if(end==std::string::npos){if(input.size()>4096)respond(431,"{}","application/json");return;}
 if(end>4096){respond(431,"{}","application/json");return;}
 auto first=input.find("\r\n"),space=input.find(' '),second=input.find(' ',space+1);
 if(first==std::string::npos||space>first||second>first){respond(400,"{}","application/json");return;}
 std::string method=input.substr(0,space),path=input.substr(space+1,second-space-1),request_key;size_t length=0;bool has_length=false;
 for(size_t start=first+2;start<end;){auto finish=input.find("\r\n",start);auto colon=input.find(':',start);if(colon>=finish){respond(400,"{}","application/json");return;}
  auto name=input.substr(start,colon-start),value=input.substr(colon+1,finish-colon-1);while(!value.empty()&&value.front()==' ')value.erase(0,1);std::transform(name.begin(),name.end(),name.begin(),[](unsigned char c){return std::tolower(c);});
  if(name=="content-length"){if(has_length||value.empty()||value.size()>6||value.find_first_not_of("0123456789")!=std::string::npos){respond(400,"{}","application/json");return;}length=std::strtoul(value.c_str(),nullptr,10);has_length=true;}
  if(name=="transfer-encoding"){respond(400,"{}","application/json");return;}
  if(name=="x-setup-key")request_key=value;start=finish+2;
 }
 if(length>24576){respond(413,"{}","application/json");return;}if(input.size()<end+4+length)return;
 if(method=="GET"&&path=="/"){respond(200,page,"text/html; charset=utf-8",true);return;}
 if(!callback||request_key!=key){respond(403,"{\"error\":\"Open setup on the device and use its link\"}","application/json");return;}
 expires=lv_tick_get()+600000;
 // Heap-owned buffers, never retained by the app. HTTP callbacks are UI-thread
 // synchronous; stop/unload clears the callback before dropping the package.
 std::string reply(32768,'\0');int status=callback(method.c_str(),path.c_str(),input.substr(end+4,length).c_str(),&reply[0],reply.size(),user);
 auto size=reply.find('\0');if(size==std::string::npos||status<200||status>599){respond(500,"{}","application/json");return;}
 reply.resize(size);respond(status,reply,"application/json; charset=utf-8");
}
}
bool server_start(){if(active)return true;
#if defined(ARDUINO)
 if(WiFi.status()!=WL_CONNECTED)return false;server.begin();
#else
 server=socket(AF_INET,SOCK_STREAM,0);if(server<0)return false;int yes=1;setsockopt(server,SOL_SOCKET,SO_REUSEADDR,&yes,sizeof yes);sockaddr_in a{};a.sin_family=AF_INET;const char*ephemeral=std::getenv("GADGET_OS_SETUP_EPHEMERAL_PORT");a.sin_port=htons(ephemeral&&std::string(ephemeral)=="1"?0:8080);a.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
 if(bind(server,reinterpret_cast<sockaddr*>(&a),sizeof a)||listen(server,2)){close(server);server=-1;return false;}fcntl(server,F_SETFL,O_NONBLOCK);
#endif
 active=true;return true;
}
void stop(){callback=nullptr;user=nullptr;page.clear();key.clear();if(!active)return;drop();
#if defined(ARDUINO)
 server.end();
#else
 close(server);server=-1;
#endif
 active=false;std::string().swap(input);std::string().swap(output);std::string().swap(page);std::string().swap(key);
}
std::string server_address(){
#if defined(ARDUINO)
 return "http://"+std::string(WiFi.localIP().toString().c_str())+":8080";
#else
 sockaddr_in address{};socklen_t size=sizeof address;
 int port=getsockname(server,reinterpret_cast<sockaddr*>(&address),&size)==0?ntohs(address.sin_port):8080;
 return "http://127.0.0.1:"+std::to_string(port);
#endif
}
void poll(){if(!active)return;uint32_t now=lv_tick_get();if(int32_t(now-expires)>=0){stop();return;}
 if(!peer_open()){
#if defined(ARDUINO)
 peer=server.accept();
#else
 peer=accept(server,nullptr,nullptr);if(peer>=0){fcntl(peer,F_SETFL,O_NONBLOCK);
#ifdef SO_NOSIGPIPE
 int yes=1;setsockopt(peer,SOL_SOCKET,SO_NOSIGPIPE,&yes,sizeof yes);
#endif
 }
#endif
 if(!peer_open())return;deadline=now+5000;
 }
 if(int32_t(now-deadline)>=0){drop();return;}
 if(output.empty()){char b[1024];int n=0;
#if defined(ARDUINO)
 if(peer.available())n=peer.read(reinterpret_cast<uint8_t*>(b),std::min<int>(peer.available(),sizeof b));else if(!peer.connected()){drop();return;}
#else
 n=recv(peer,b,sizeof b,0);if(n==0||(n<0&&errno!=EAGAIN&&errno!=EWOULDBLOCK)){drop();return;}
#endif
 if(n>0){input.append(b,n);if(input.size()>28672)respond(413,"{}","application/json");else parse();}
 }
 if(!output.empty()){
#if defined(ARDUINO)
 int n=peer.write(reinterpret_cast<const uint8_t*>(output.data()+sent),std::min<size_t>(1024,output.size()-sent));
#else
 int flags=0;
#ifdef MSG_NOSIGNAL
 flags=MSG_NOSIGNAL;
#endif
 int n=send(peer,output.data()+sent,std::min<size_t>(1024,output.size()-sent),flags);if(n<0&&errno!=EAGAIN&&errno!=EWOULDBLOCK){drop();return;}
#endif
 if(n>0)sent+=n;if(sent==output.size())drop();
 }
}
uint32_t is_active(){return active;}
int32_t start(const void*html,uint32_t size,gadget_setup_callback cb,void*u,char*out,uint32_t cap){
 if(!html||!size||size>16384||!cb||!out||cap<96||cap>256)return -1;
 stop();if(!server_start())return -1;page.assign((const char*)html,size);callback=cb;user=u;
 for(int i=0;i<4;++i){uint32_t n;
#ifdef ARDUINO
 n=esp_random();
#else
 n=std::random_device{}();
#endif
 char b[9];snprintf(b,sizeof b,"%08x",unsigned(n));key+=b;
 }
 auto url=server_address()+"/#"+key;if(url.size()>=cap){stop();return -1;}
 memcpy(out,url.c_str(),url.size()+1);expires=lv_tick_get()+600000;return url.size();
}

}

#endif
