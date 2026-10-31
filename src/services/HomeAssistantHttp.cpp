#include "services/HomeAssistantHttp.h"
#include "services/HomeAssistantEntityStream.h"
#include <algorithm>
#if defined(ARDUINO)
#include <Arduino.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#else
#include <cstdio>
#include <cstdlib>
#include <unistd.h>
#include <sys/wait.h>
#include <signal.h>
#endif
namespace ha {
bool request(const Config&c,const std::string&path,const std::string&payload,std::string&out,std::string&error,EntityStream*discovery,int*response_status){
 out.clear();int status=0;if(response_status)*response_status=0;
#if defined(ARDUINO)
 struct Body:Stream {EntityStream*discovery=nullptr;bool too_large=false;uint32_t started=millis();std::string data;size_t write(uint8_t v)override{return write(&v,1);}size_t write(const uint8_t*b,size_t n)override{if(discovery){if(millis()-started>30000){discovery->error="Entity discovery timed out";return 0;}return discovery->feed(reinterpret_cast<const char*>(b),n)?n:0;}if(data.size()+n>262144){too_large=true;return 0;}data.append(reinterpret_cast<const char*>(b),n);return n;}int available()override{return 0;}int read()override{return -1;}int peek()override{return -1;}void flush()override{}} sink;sink.discovery=discovery;
 WiFiClient plain;WiFiClientSecure tls;
 if(c.ca.empty())tls.useBuiltinCACertBundle();else tls.setCACert(c.ca.c_str());tls.setHandshakeTimeout(5);
 HTTPClient http;http.setConnectTimeout(3000);http.setTimeout(4000);http.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);
 bool ok=c.url.compare(0,8,"https://")==0?http.begin(tls,(c.url+path).c_str()):http.begin(plain,(c.url+path).c_str());
 if(ok){http.addHeader("Authorization",("Bearer "+c.token).c_str());http.addHeader("Content-Type","application/json");status=payload.empty()?http.GET():http.POST(String(payload.c_str()));
  if(status>=200&&status<300){if(!discovery&&http.getSize()>262144)status=-2;else if(http.writeToStream(&sink)<0)status=sink.too_large||(discovery&&!discovery->error.empty())?-2:-1;else out=std::move(sink.data);}http.end();}
#else
 // No shell and no bearer token in process arguments. curl reads its config
 // from a private temporary file; unlink it immediately after opening in child.
 auto quote=[](const std::string&s){std::string q="\"";for(char ch:s){if(ch=='\\'||ch=='\"')q+='\\';if(ch=='\n')q+="\\n";else if(ch=='\r')q+="\\r";else q+=ch;}return q+'"';};
 char caPath[]="/tmp/gadget-ha-ca-XXXXXX";int ca=-1;
 if(!c.ca.empty()){ca=mkstemp(caPath);if(ca<0){error="Cannot prepare TLS certificate";return false;}write(ca,c.ca.data(),c.ca.size());close(ca);}
 std::string cfg="silent\nmax-time = "+std::string(discovery?"30":"8")+"\nmax-filesize = "+std::string(discovery?"16777216":"262144")+"\nurl = "+quote(c.url+path)+"\nheader = "+quote("Authorization: Bearer "+c.token)+"\nheader = \"Content-Type: application/json\"\nwrite-out = \"\\n%{http_code}\"\n";
 if(ca>=0)cfg+="cacert = "+quote(caPath)+"\n";
 if(!payload.empty())cfg+="data = "+quote(payload)+"\n";
 char configPath[]="/tmp/gadget-ha-request-XXXXXX";int fd=mkstemp(configPath);
 if(fd<0){if(ca>=0)unlink(caPath);error="Cannot prepare request";return false;}
 if(write(fd,cfg.data(),cfg.size())!=ssize_t(cfg.size())){close(fd);unlink(configPath);if(ca>=0)unlink(caPath);error="Cannot prepare request";return false;}
 lseek(fd,0,SEEK_SET);unlink(configPath);int pipes[2];if(pipe(pipes)!=0){close(fd);if(ca>=0)unlink(caPath);error="Cannot start request";return false;}
 pid_t pid=fork();if(pid==0){dup2(fd,STDIN_FILENO);dup2(pipes[1],STDOUT_FILENO);close(fd);close(pipes[0]);close(pipes[1]);execlp("curl","curl","-q","--config","-",static_cast<char*>(nullptr));_exit(127);}
 close(fd);close(pipes[1]);bool large=false;std::string tail;char b[2048];ssize_t n;
 if(pid>0)while((n=read(pipes[0],b,sizeof b))>0){if(discovery){tail.append(b,n);if(tail.size()>4){discovery->feed(tail.data(),tail.size()-4);tail.erase(0,tail.size()-4);}}else if(out.size()+n<=262160)out.append(b,n);else large=true;}
 close(pipes[0]);int result=0;if(pid>0)waitpid(pid,&result,0);if(ca>=0)unlink(caPath);
 if(discovery)out=tail;auto end=out.rfind('\n');if(pid>0&&WIFEXITED(result)&&WEXITSTATUS(result)==0&&!large&&end!=std::string::npos){status=std::atoi(out.c_str()+end+1);out.resize(end);}if(large||(pid>0&&WIFEXITED(result)&&WEXITSTATUS(result)==63)){status=-2;if(discovery)discovery->error="Entity discovery exceeds 16 MiB";}
#endif
 if(response_status)*response_status=status;
 if(discovery&&((status>=200&&status<300)||status==-2)&&!discovery->error.empty()){error=discovery->error;out.clear();return false;}
 if(status>=200&&status<300){if(discovery&&!discovery->finish()){error=discovery->error;return false;}error.clear();return true;}
 out.clear();error=status==401||status==403?"Home Assistant access denied":status==404?"Entity no longer exists":status==-2?"Home Assistant response too large":status>=300&&status<400?"Use the direct Home Assistant address (no redirects)":"Home Assistant unavailable - check address, Wi-Fi and TLS";return false;
}
}
