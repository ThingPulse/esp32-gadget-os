#include "ui/I18n.h"
#include "services/StockService.h"
#include "services/ConfigStoreService.h"
#include "services/WifiService.h"
#include "services/TimeService.h"
#include "services/UpdateService.h"
#include <algorithm>
#include <vector>
#if defined(ARDUINO)
#include <Arduino.h>
#include <esp_random.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#else
#include <chrono>
#include <thread>
#include <cmath>
#include <cstdlib>
#endif

namespace {
stocks::YahooStockProvider yahoo;
#if !defined(ARDUINO)
class TutorialStockProvider : public stocks::StockDataProvider {
public:
    const char *id() const override { return "yahoo"; }
    const char *name() const override { return i18n::tr("Demo quotes"); }
    stocks::Result fetch(const std::string &symbol, stocks::Period period) override {
        std::this_thread::sleep_for(std::chrono::milliseconds(40));
        stocks::Result result;
        auto &q = result.quote;
        const char *symbols[] = {"AAPL", "NESN.SW", "7203.T", "SAP.DE", "VOD.L", "BTC-USD"};
        const char *names[] = {"Apple", "Nestle", "Toyota", "SAP", "Vodafone", "Bitcoin"};
        const char *currencies[] = {"USD", "CHF", "JPY", "EUR", "GBp", "USD"};
        const char *exchanges[] = {"NASDAQ", "SIX", "Tokyo", "XETRA", "London", "Crypto"};
        const double prices[] = {230, 80, 2750, 220, 75, 68000};
        size_t item = 0;
        for (size_t i = 0; i < 6; ++i) if (symbol == symbols[i]) item = i;
        q.symbol = symbol; q.name = names[item]; q.currency = currencies[item]; q.exchange = exchanges[item];
        q.price = prices[item]; q.previous_close = q.price / 1.0132; q.has_previous_close = true;
        q.timestamp = 1792767600; q.period = period;
        q.count = period == stocks::Period::FiveDays ? 5 : period == stocks::Period::Month ? 22 : 90;
        for (size_t i = 0; i < q.count; ++i) {
            const double t = double(i) / (q.count - 1);
            q.samples[i].time = q.timestamp - (q.count - 1 - i) * 86400;
            q.samples[i].price = q.price * (0.92 + 0.08*t + 0.014*std::sin(t*22) + 0.007*std::sin(t*57));
        }
        q.samples[q.count-1].price = q.price;
        return result;
    }
} tutorial_provider;
#endif
}
StockService::StockService():_provider(&yahoo) {}
StockService &StockService::instance() { static StockService s; return s; }
uint32_t StockService::now_ms() {
#if defined(ARDUINO)
    return millis();
#else
    return uint32_t(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
#endif
}
bool StockService::begin() {
#if !defined(ARDUINO)
    if (std::getenv("GADGET_OS_TUTORIAL_PATH") && std::getenv("GADGET_OS_STOCK_FIXTURE"))
        _provider = &tutorial_provider;
#endif
    auto &cfg=ConfigStoreService::instance();
    if(!cfg.begin() || !cfg.load("stocks")) return false;
    std::array<std::string,stocks::MaxSymbols> symbols; size_t count=0; std::string error;
    if(!stocks::parse_watchlist(cfg.get("symbols","AAPL\nNESN.SW\n7203.T\nSAP.DE\nVOD.L"),symbols,count,error)) return false;
    _mapping_provider=cfg.get("provider","yahoo");
    _count=count;
    for(size_t i=0;i<count;++i) _entries[i].symbol=symbols[i];
    return true;
}
std::string StockService::watchlist_text() const {
    std::string text; for(size_t i=0;i<_count;++i) { if(i)text+='\n'; text+=_entries[i].symbol; } return text;
}
bool StockService::save_watchlist(const std::string &text,std::string &error) {
    std::array<std::string,stocks::MaxSymbols> symbols; size_t count=0;
    if(!stocks::parse_watchlist(text,symbols,count,error)) return false;
    auto &cfg=ConfigStoreService::instance();
    if(!cfg.load("stocks")) { error=i18n::tr("Cannot open settings"); return false; }
    std::string normalized; for(size_t i=0;i<count;++i) { if(i)normalized+='\n'; normalized+=symbols[i]; }
    cfg.set("symbols",normalized); cfg.set("provider",std::string(_provider->id()));
    if(!cfg.save()) { error=i18n::tr("Could not save watchlist"); return false; }
    std::vector<Entry> entries(count);
    for(size_t i=0;i<count;++i) {
        entries[i].symbol=symbols[i];
        if(_mapping_provider==_provider->id()) for(size_t j=0;j<_count;++j)
            if(_entries[j].symbol==symbols[i]) entries[i]=_entries[j];
    }
    for(size_t i=0;i<count;++i) _entries[i]=std::move(entries[i]);
    for(size_t i=count;i<_count;++i) _entries[i]=Entry{};
    _count=count; _selected=0;
    _mapping_provider=_provider->id(); ++_generation; ++_revision; return true;
}
bool StockService::set_provider(stocks::StockDataProvider &provider) {
    if(_running) return false;
    _provider=&provider; ++_generation;
    for(auto &e:_entries) { std::string symbol=e.symbol; e=Entry{}; e.symbol=symbol; }
    _gate_due=true; ++_revision; return true;
}
void StockService::select(size_t index,stocks::Period period) {
    if(index>=_count || !_provider->supports(period)) return; _selected=index;
    auto &e=_entries[index];
    if(e.desired!=period) { e.desired=period;
        if(e.error.empty() || int32_t(now_ms()-e.deadline)>=0) e.due=true;
        ++_revision; }
}
StockService::Refresh StockService::refresh(size_t index) {
    if(index>=_count || _mapping_provider!=_provider->id()) return Refresh::RetryLater;
    auto &e=_entries[index];
    const uint32_t now=now_ms();
    if(!e.error.empty() && int32_t(now-e.deadline)<0) return Refresh::RetryLater;
    if(e.refresh_pending) return Refresh::Queued;
    if(e.valid && now-e.fetched<30000U) return Refresh::Recent;
    e.refresh_pending=true; e.due=true; ++_revision;
    // Keep the global request gate, including Retry-After and backoff.
    return Refresh::Queued;
}
StockService::Refresh StockService::refresh_all() {
    Refresh result=Refresh::Recent;
    for(size_t i=0;i<_count;++i) {
        const auto r=refresh(i);
        if(r==Refresh::Queued || (r==Refresh::RetryLater && result!=Refresh::Queued)) result=r;
    }
    return result;
}
void StockService::worker(void *data) {
    auto *s=static_cast<StockService *>(data);
    s->_result=s->_provider->fetch(s->_request_symbol,s->_request_period);
    s->_finished.store(true,std::memory_order_release);
#if defined(ARDUINO)
    vTaskDelete(nullptr);
#endif
}
void StockService::loop() {
    const uint32_t now=now_ms();
    if(_running && _finished.load(std::memory_order_acquire)) {
        _running=false;
        uint32_t delay=2000;
#if !defined(ARDUINO)
        // Offline tutorial data has no remote provider rate limit.
        if (_provider == &tutorial_provider) delay=100;
#endif
        if(_request_generation==_generation && _request_index<_count &&
           _entries[_request_index].symbol==_request_symbol) {
            auto &e=_entries[_request_index];
            e.refresh_pending=false;
            if(_result.ok()) {
                e.quote=std::move(_result.quote); e.valid=true; e.error.clear(); e.fetched=now; e.backoff=60000;
                e.deadline=now+300000; e.due=e.desired!=_request_period;
            } else {
                e.error=_result.message;
                delay=e.backoff;
                if(_result.error==stocks::Error::InvalidSymbol || _result.error==stocks::Error::Authentication) delay=21600000;
                delay=std::max(delay,_result.retry_after_ms);
                e.backoff=std::min<uint32_t>(e.backoff*2U,21600000U);
                e.deadline=now+delay; e.due=false;
                if(_result.error==stocks::Error::InvalidSymbol || _result.error==stocks::Error::InvalidData) delay=2000;
            }
#if defined(ARDUINO)
            e.deadline+=esp_random()%15001U;
#endif
            ++_revision;
        }
        _result=stocks::Result{};
        _gate=now+delay; _gate_due=false;
    }
    // Latch deadlines even while the app is closed or Wi-Fi is offline.
    for(size_t i=0;i<_count;++i) if(int32_t(now-_entries[i].deadline)>=0) _entries[i].due=true;
    if(int32_t(now-_gate)>=0) _gate_due=true;
    if(!_active || _running || !_gate_due || !_count) return;
    if(_mapping_provider!=_provider->id()) {
        for(size_t i=0;i<_count;++i) _entries[i].error=i18n::tr("Re-save symbols for selected provider");
        return;
    }
#if defined(ARDUINO)
    if(!WifiService::instance().is_connected() || !TimeService::instance().is_synchronized()) return;
    const auto ota=UpdateService::instance().state();
    if(ota==UpdateService::State::Checking || ota==UpdateService::State::Downloading || ota==UpdateService::State::ReadyToReboot) return;
#endif
    size_t index=_count;
    if(_selected<_count && _entries[_selected].due) index=_selected;
    else for(size_t i=0;i<_count;++i) if(_entries[i].due) { index=i; break; }
    if(index==_count) return;
    _request_index=index; _request_symbol=_entries[index].symbol;
    _request_period=_entries[index].desired; _request_generation=_generation;
    _finished.store(false,std::memory_order_relaxed); _running=true; ++_revision;
#if defined(ARDUINO)
    if(xTaskCreate(worker,"stock_quote",12288,this,1,nullptr)!=pdPASS) {
        _result.error=stocks::Error::Unavailable; _result.message=i18n::tr("Not enough memory for request");
        _finished.store(true,std::memory_order_release);
    }
#else
    std::thread(worker,this).detach();
#endif
}
SERVICE_REGISTER(StockService);
