#include "ui/I18n.h"
#include "services/StockDataProvider.h"
#include "ui/AsciiFold.h"
#include <ArduinoJson.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#if defined(ARDUINO)
#include <Arduino.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#endif

namespace stocks {
namespace {
constexpr size_t MaxBody = 64 * 1024;
Result failure(Error error, const char *message) { Result r; r.error=error; r.message=message; return r; }
bool number(JsonVariantConst v) { return v.is<double>() && std::isfinite(v.as<double>()); }
#if defined(ARDUINO)
class BoundedBody : public Stream {
public:
    std::string body;
    size_t write(uint8_t b) override { return write(&b, 1); }
    size_t write(const uint8_t *data, size_t count) override {
        if (body.size() + count > MaxBody) return 0;
        body.append(reinterpret_cast<const char *>(data), count); return count;
    }
    int available() override { return 0; }
    int read() override { return -1; }
    int peek() override { return -1; }
    void flush() override {}
};
#endif
}
bool valid_symbol(const std::string &s) {
    if (s.empty() || s.size()>24) return false;
    for (char c:s) if (!((c>='A' && c<='Z') || (c>='0' && c<='9') || c=='.' || c=='-')) return false;
    return s.front()!='.' && s.front()!='-';
}
bool parse_watchlist(const std::string &text, std::array<std::string, MaxSymbols> &out,
                     size_t &count, std::string &error) {
    std::array<std::string, MaxSymbols> parsed{}; size_t n=0;
    size_t cursor=0;
    while(cursor<text.size()) {
        const size_t start=text.find_first_not_of(" \t\r\n",cursor);
        if(start==std::string::npos) break;
        const size_t end=text.find_first_of(" \t\r\n",start);
        std::string s=text.substr(start,end==std::string::npos?end:end-start);
        cursor=end==std::string::npos?text.size():end;
        for (char &c:s) if (c>='a' && c<='z') c-=32;
        if (!valid_symbol(s)) { error=i18n::tr("Use stock symbols, e.g. AAPL or NESN.SW"); return false; }
        if (n==MaxSymbols) { error=i18n::tr("Maximum 10 symbols"); return false; }
        for (size_t i=0;i<n;++i) if (parsed[i]==s) { error=i18n::tr("Duplicate symbol: ")+s; return false; }
        parsed[n++]=s;
    }
    out=parsed; count=n; error.clear(); return true;
}
const char *period_text(Period p) { return p==Period::FiveDays?i18n::tr("5D"):p==Period::Month?"1M":i18n::tr("1Y"); }
std::string price_text(double value,const std::string &currency) {
    char b[64]; std::snprintf(b,sizeof(b),currency=="JPY"?"%.0f %s":"%.2f %s",value,currency.c_str()); return b;
}
std::string change_text(const Quote &q,bool percent_only) {
    if (!q.has_previous_close || q.previous_close<=0) return i18n::tr("Change unavailable");
    const double change=q.price-q.previous_close, percent=change/q.previous_close*100;
    char b[64]; if(percent_only) std::snprintf(b,sizeof(b),"%+.2f%%",percent);
    else std::snprintf(b,sizeof(b),"%+.2f  (%+.2f%%)",change,percent);
    return b;
}
Result YahooStockProvider::parse(const std::string &body,const std::string &symbol,Period period) {
    if(body.size()>MaxBody) return failure(Error::InvalidData,i18n::tr("Response too large"));
    JsonDocument filter;
    JsonObject f=filter["chart"]["result"][0].to<JsonObject>();
    f["meta"]=true; f["timestamp"]=true; f["indicators"]["quote"][0]["close"]=true;
    filter["chart"]["error"]=true;
    JsonDocument doc;
    if(deserializeJson(doc,body,DeserializationOption::Filter(filter),DeserializationOption::NestingLimit(12)))
        return failure(Error::InvalidData,i18n::tr("Invalid provider response"));
    if(!doc["chart"]["error"].isNull()) return failure(Error::InvalidSymbol,i18n::tr("Symbol unavailable at Yahoo"));
    JsonObjectConst root=doc["chart"]["result"][0]; JsonObjectConst meta=root["meta"];
    if(meta.isNull() || std::string(meta["symbol"]|"")!=symbol || !number(meta["regularMarketPrice"]) ||
       meta["regularMarketPrice"].as<double>()<=0 || !meta["regularMarketTime"].is<int64_t>() ||
       meta["regularMarketTime"].as<int64_t>()<=0 || !meta["currency"].is<const char *>())
        return failure(Error::InvalidData,i18n::tr("Incomplete quote"));
    Result r; auto &q=r.quote; q.symbol=symbol; q.period=period;
    q.name=ascii_fold_latin(meta["longName"]|meta["shortName"]|symbol.c_str());
    q.exchange=meta["fullExchangeName"]|meta["exchangeName"]|"";
    q.currency=meta["currency"].as<std::string>();
    if(q.currency.empty() || q.currency.size()>8) return failure(Error::InvalidData,i18n::tr("Invalid currency"));
    q.name=q.name.substr(0,80); q.exchange=q.exchange.substr(0,40);
    q.price=meta["regularMarketPrice"]; q.timestamp=meta["regularMarketTime"];
    // chartPreviousClose is the close BEFORE THE REQUESTED RANGE, not yesterday.
    if(number(meta["previousClose"]) && meta["previousClose"].as<double>()>0) {
        q.previous_close=meta["previousClose"]; q.has_previous_close=true;
    }
    if(meta["exchangeDataDelayedBy"].is<int>()) q.delay_minutes=meta["exchangeDataDelayedBy"];
    JsonArrayConst times=root["timestamp"], closes=root["indicators"]["quote"][0]["close"];
    if(times.size()!=closes.size()) return failure(Error::InvalidData,i18n::tr("Inconsistent chart"));
    int64_t last=0;
    for(JsonVariantConst t:times) {
        if(!t.is<int64_t>() || t.as<int64_t>()<=last) return failure(Error::InvalidData,i18n::tr("Unsorted chart"));
        last=t.as<int64_t>();
    }
    // Daily history supplies the previous trading session close when metadata
    // omits it. Exclude the latest quote's exchange-local calendar day.
    if(!q.has_previous_close && meta["gmtoffset"].is<int>()) {
        const int offset=meta["gmtoffset"];
        const int64_t today=(q.timestamp+offset)/86400;
        for(size_t i=0;i<times.size();++i) if(times[i].is<int64_t>() && number(closes[i]) &&
            (times[i].as<int64_t>()+offset)/86400<today && closes[i].as<double>()>0) {
            q.previous_close=closes[i]; q.has_previous_close=true;
        }
    }
    const size_t n=std::min(times.size(),MaxSamples);
    int64_t previous=0;
    for(size_t i=0;i<n;++i) {
        size_t index=n>1?i*(times.size()-1)/(n-1):0;
        if(!times[index].is<int64_t>()) return failure(Error::InvalidData,i18n::tr("Invalid chart time"));
        const int64_t t=times[index]; if(t<=previous) return failure(Error::InvalidData,i18n::tr("Unsorted chart"));
        previous=t; q.samples[i].time=t;
        if(number(closes[index]) && closes[index].as<double>()>0) q.samples[i].price=closes[index];
    }
    q.count=n; return r;
}
Result YahooStockProvider::fetch(const std::string &symbol,Period period) {
    if(!valid_symbol(symbol)) return failure(Error::InvalidSymbol,i18n::tr("Invalid symbol"));
    const char *range=period==Period::FiveDays?"5d":period==Period::Month?"1mo":"1y";
    const std::string url="https://query1.finance.yahoo.com/v8/finance/chart/"+symbol+
        "?range="+range+"&interval=1d&includePrePost=false";
    int status=0; std::string body; uint32_t retry=0;
#if defined(ARDUINO)
    WiFiClientSecure client; client.useBuiltinCACertBundle(); client.setHandshakeTimeout(8);
    HTTPClient http; http.setConnectTimeout(5000); http.setTimeout(8000);
    if(!http.begin(client,url.c_str())) return failure(Error::Unavailable,i18n::tr("Connection failed"));
    const char *headers[]={"Retry-After"}; http.collectHeaders(headers,1);
    http.addHeader("User-Agent","ThingPulse-GadgetOS/1.0");
    status=http.GET();
    long seconds=http.header("Retry-After").toInt();
    if(seconds>0) retry=uint32_t(std::min(seconds,21600L))*1000U;
    if(status==200) {
        BoundedBody sink;
        if(http.getSize()>int(MaxBody) || http.writeToStream(&sink)<0) status=-1;
        else body=std::move(sink.body);
    }
    http.end();
#else
    // Symbol alphabet is validated above; no user shell syntax can reach this URL.
    const std::string command="curl -sS --max-time 15 --max-filesize 65536 -A 'ThingPulse-GadgetOS/1.0' -w '\\n%{http_code}' '"+url+"'";
    FILE *pipe=popen(command.c_str(),"r"); if(!pipe) return failure(Error::Unavailable,i18n::tr("Cannot start request"));
    char buffer[1024]; bool oversized=false;
    while(size_t n=fread(buffer,1,sizeof(buffer),pipe)) {
        if(body.size()+n<=MaxBody+16) body.append(buffer,n); else oversized=true;
    }
    const int exit=pclose(pipe); auto pos=body.rfind('\n');
    if(exit==0 && !oversized && pos!=std::string::npos) { status=std::atoi(body.c_str()+pos+1); body.resize(pos); }
#endif
    if(status==200) return parse(body,symbol,period);
    Result r=failure(status==429?Error::Throttled:status==401||status==403?Error::Authentication:
                     status==404?Error::InvalidSymbol:Error::Unavailable,
                     status==429?i18n::tr("Yahoo rate limit"):status==404?i18n::tr("Symbol not found"):
                     status==401||status==403?i18n::tr("Yahoo denied access"):i18n::tr("Quote request failed"));
    r.retry_after_ms=retry; return r;
}
}
