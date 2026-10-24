#include "services/TransitProvider.h"
#include "services/DeviceCheckInPolicy.h"
#include "SystemVersion.h"
#include "ui/AsciiFold.h"
#include <ArduinoJson.h>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#if !defined(ARDUINO)
#include <fstream>
#include <iterator>
#endif
#if defined(ARDUINO)
#include <Arduino.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#endif
namespace transit {
namespace {
constexpr size_t MaxBody=192*1024;
Result fail(const char *s) {Result r;r.error=s;return r;}
std::string text(JsonVariantConst v,size_t max=96) {return v.is<const char *>()?ascii_fold_latin(v.as<std::string>()).substr(0,max):"";}
std::string raw(JsonVariantConst v) {return v.is<const char *>()?v.as<std::string>().substr(0,512):"";}
#if defined(ARDUINO)
class Body : public Stream {
public: std::string data;
 size_t write(uint8_t b) override {return write(&b,1);}
 size_t write(const uint8_t *b,size_t n) override {if(data.size()+n>MaxBody)return 0;data.append((const char *)b,n);return n;}
 int available() override{return 0;} int read() override{return -1;} int peek() override{return -1;} void flush() override{}
};
#endif
}
std::string encode(const std::string &s) {
 const char *hex="0123456789ABCDEF";std::string out;
 for(unsigned char c:s) {if((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='-'||c=='_'||c=='.'||c=='~')out+=c;
 else {out+='%';out+=hex[c>>4];out+=hex[c&15];}}return out;
}
int64_t timestamp(const std::string &s) {
 // ISO-8601 UTC/offset timestamps, independent of device timezone and time_t width.
 if(s.size()<20)return 0;
 int y,m,d,h,mi,se,n=0;
 if(std::sscanf(s.c_str(),"%4d-%2d-%2dT%2d:%2d:%2d%n",&y,&m,&d,&h,&mi,&se,&n)!=6 || n!=19 || y<1970 || y>2100 || m<1 || m>12 || d<1 || h<0 || h>23 || mi<0 || mi>59 || se<0 || se>59)return 0;
 const int days[]={31,28,31,30,31,30,31,31,30,31,30,31};
 if(d>days[m-1]+(m==2 && y%4==0 && (y%100!=0 || y%400==0)))return 0;
 size_t p=19;if(p<s.size()&&s[p]=='.'){++p;size_t first=p;while(p<s.size()&&s[p]>='0'&&s[p]<='9')++p;if(p==first)return 0;}
 int offset=0;
 if(p<s.size()&&s[p]=='Z'&&p+1==s.size()){}
 else if(p+6==s.size()&&(s[p]=='+'||s[p]=='-')&&s[p+3]==':') {
  for(size_t i:{p+1,p+2,p+4,p+5})if(s[i]<'0'||s[i]>'9')return 0;
  int oh=(s[p+1]-'0')*10+s[p+2]-'0',om=(s[p+4]-'0')*10+s[p+5]-'0';if(oh>23||om>59)return 0;
  offset=(oh*60+om)*60*(s[p]=='+'?1:-1);
 }else return 0;
 y-=m<=2;int era=y/400;unsigned yoe=unsigned(y-era*400);unsigned doy=(153*(m+(m>2?-3:9))+2)/5+d-1;unsigned doe=yoe*365+yoe/4-yoe/100+doy;
 return (int64_t(era)*146097+doe-719468)*86400+h*3600+mi*60+se-offset;
}
std::string clock_text(int64_t value) {if(!value)return "--:--";time_t t=static_cast<time_t>(value);tm local{};localtime_r(&t,&local);char b[16];strftime(b,sizeof(b),"%H:%M",&local);return b;}
std::string countdown(const Departure &d,int64_t now) {
 if(d.cancelled)return "Cancelled";if(now<1600000000)return "-- min";
 if(d.time<now-60)return "Departed";if(d.time<=now+30)return "Due";return std::to_string((d.time-now+59)/60)+" min";
}
Result TransitousProvider::parse(Request kind,const std::string &body) {
 if(body.size()>MaxBody)return fail("Response too large");
 JsonDocument filter,doc;
 if(kind==Request::Search) {for(const char *k:{"type","id","name","country"})filter[0][k]=true;}
 else if(kind==Request::Board) {
  auto f=filter["stopTimes"][0].to<JsonObject>();
  for(const char *k:{"displayName","routeShortName","headsign","tripId","realTime","cancelled","tripCancelled","routeColor"})f[k]=true;
  f["tripTo"]["name"]=true;
  for(const char *k:{"departure","scheduledDeparture","track","scheduledTrack","cancelled"})f["place"][k]=true;
 }else {
  auto f=filter["legs"][0].to<JsonObject>();
  for(const char *key:{"from","to","intermediateStops"}) {
   JsonObject o=std::string(key)=="intermediateStops"?f[key][0].to<JsonObject>():f[key].to<JsonObject>();
   for(const char *k:{"stopId","parentId","name","departure","arrival","scheduledDeparture","scheduledArrival","track","cancelled"})o[k]=true;
  }
 }
 if(deserializeJson(doc,body,DeserializationOption::Filter(filter),DeserializationOption::NestingLimit(16)))return fail("Invalid transit response");
 Result r;
 if(kind==Request::Search) {
  if(!doc.is<JsonArray>())return fail("Invalid stop search");
  for(JsonObjectConst o:doc.as<JsonArrayConst>()) {if(r.stops.size()==MaxStops)break;if(std::string(o["type"]|"")!="STOP")continue;
   Stop s{raw(o["id"]),text(o["name"]),text(o["country"],16)};if(!s.id.empty()&&!s.name.empty())r.stops.push_back(s);}
 }else if(kind==Request::Board) {
  if(!doc["stopTimes"].is<JsonArray>())return fail("Invalid departures");
  for(JsonObjectConst o:doc["stopTimes"].as<JsonArrayConst>()) {
   if(r.departures.size()==MaxDepartures)break;
   JsonObjectConst p=o["place"];Departure d;
   d.scheduled=timestamp(raw(p["scheduledDeparture"]));d.time=timestamp(raw(p["departure"]));if(!d.time)d.time=d.scheduled;if(!d.scheduled)d.scheduled=d.time;if(!d.time)continue;
   d.line=text(o["displayName"],20);if(d.line.empty())d.line=text(o["routeShortName"],20);if(d.line.empty())d.line="Transit";
   d.destination=text(o["headsign"]);if(d.destination.empty())d.destination=text(o["tripTo"]["name"]);
   d.trip=raw(o["tripId"]);d.platform=text(p["track"],24);if(d.platform.empty())d.platform=text(p["scheduledTrack"],24);
   d.live=o["realTime"]|false;d.cancelled=(o["cancelled"]|false)||(o["tripCancelled"]|false)||(p["cancelled"]|false);
   auto color=raw(o["routeColor"]);if(color.size()==6 && color.find_first_not_of("0123456789abcdefABCDEF")==std::string::npos)d.color=std::strtoul(color.c_str(),nullptr,16);
   r.departures.push_back(d);
  }
  std::stable_sort(r.departures.begin(),r.departures.end(),[](const Departure&a,const Departure&b){return a.time<b.time;});
 }else {
  if(!doc["legs"].is<JsonArray>())return fail("Trip details unavailable");
  auto add=[&](JsonObjectConst p){if(p.isNull()||r.calls.size()==MaxCalls)return;Call c; c.id=raw(p["stopId"]);c.parent=raw(p["parentId"]);c.name=text(p["name"]);c.platform=text(p["track"],24);c.cancelled=p["cancelled"]|false;
   for(const char *key:{"departure","arrival","scheduledDeparture","scheduledArrival"})if(!c.time)c.time=timestamp(raw(p[key]));
   if(!c.name.empty() && (r.calls.empty() || r.calls.back().id!=c.id || r.calls.back().time!=c.time))r.calls.push_back(c);};
  for(JsonObjectConst leg:doc["legs"].as<JsonArrayConst>()){add(leg["from"]);for(JsonObjectConst p:leg["intermediateStops"].as<JsonArrayConst>())add(p);add(leg["to"]);}
 }
 return r;
}
Result TransitousProvider::fetch(Request kind,const std::string &argument) {
#if !defined(ARDUINO)
 if(const char *dir=std::getenv("GADGET_OS_TRANSIT_FIXTURE_DIR")) {
  std::ifstream in(std::string(dir)+(kind==Request::Search?"/search.json":kind==Request::Board?"/board.json":"/trip.json"));
  if(!in)return fail("Missing transit fixture");
  std::string data((std::istreambuf_iterator<char>(in)),std::istreambuf_iterator<char>());return parse(kind,data);
 }
#endif
 if(argument.empty()||argument.size()>512)return fail("Invalid request");
 std::string path=kind==Request::Search?"v1/geocode?type=STOP&numResults=8&text=":kind==Request::Board?"v6/stoptimes?n=8&withAlerts=false&stopId=":"v6/trip?detailedLegs=false&tripId=";
 std::string url="https://api.transitous.org/api/"+path+encode(argument),body;
 int status=0;uint32_t retry=0;
 const char *agent="ThingPulse-GadgetOS/" GADGET_OS_VERSION " (+https://github.com/ThingPulse/esp32-gadget-os)";
#if defined(ARDUINO)
 WiFiClientSecure client;client.useBuiltinCACertBundle();client.setHandshakeTimeout(8);
 HTTPClient http;http.setConnectTimeout(5000);http.setTimeout(8000);http.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);
 if(!http.begin(client,url.c_str()))return fail("Connection failed");
 const char *headers[]={"Retry-After"};http.collectHeaders(headers,1);http.addHeader("User-Agent",agent);status=http.GET();
 retry=DeviceCheckInPolicy::retry_after_ms(http.header("Retry-After").c_str());
 if(status==200){Body sink;if(http.getSize()>int(MaxBody)||http.writeToStream(&sink)<0)status=-1;else body=std::move(sink.data);}http.end();
#else
 // Every variable URL byte is percent-encoded. Fixed endpoint; no shell input.
 std::string cmd="curl -sS --max-time 15 --max-filesize 196608 -A '"+std::string(agent)+"' -w '\\n%{http_code}' '"+url+"'";
 FILE *p=popen(cmd.c_str(),"r");if(!p)return fail("Cannot start request");char buffer[2048];bool large=false;
 while(size_t n=fread(buffer,1,sizeof(buffer),p)){if(body.size()+n<=MaxBody+16)body.append(buffer,n);else large=true;}
 int exit=pclose(p);size_t end=body.rfind('\n');if(exit==0&&!large&&end!=std::string::npos){status=std::atoi(body.c_str()+end+1);body.resize(end);}
#endif
 if(status==200)return parse(kind,body);
 Result r=fail(status==429?"Transitous rate limit":status==404?"Stop or trip no longer available":status==401||status==403?"Transitous access denied":"Transitous unavailable - retry later");
 r.retry_ms=retry;return r;
}
}
