#pragma once
#include "ServiceRegistry.h"
#include "services/StockDataProvider.h"
#include <atomic>

class StockService : public SystemService {
public:
    struct Entry {
        std::string symbol;
        stocks::Quote quote;
        bool valid=false;
        std::string error;
        uint32_t fetched=0, deadline=0, backoff=60000;
        bool due=true, refresh_pending=false;
        stocks::Period desired=stocks::Period::FiveDays;
    };
    static StockService &instance();
    const char *name() const override { return "Stocks"; }
    bool begin() override;
    void loop() override;
    void resume() { _active=true; }
    void pause() { _active=false; }
    size_t count() const { return _count; }
    const Entry &entry(size_t i) const { return _entries[i]; }
    uint32_t revision() const { return _revision; }
    bool busy() const { return _running; }
    bool fetching(size_t index) const { return _running && _request_generation==_generation && _request_index==index; }
    const char *provider_name() const { return _provider->name(); }
    bool supports(stocks::Period period) const { return _provider->supports(period); }
    std::string watchlist_text() const;
    bool save_watchlist(const std::string &text, std::string &error);
    void select(size_t index, stocks::Period period);
    enum class Refresh { Queued, Recent, RetryLater };
    Refresh refresh(size_t index);
    Refresh refresh_all();
    // Replacement adapters are static-lifetime objects. Existing symbols are
    // provider-qualified; explicit watchlist save is required to remap them.
    bool set_provider(stocks::StockDataProvider &provider);
    static uint32_t now_ms();
private:
    StockService();
    static void worker(void *self);
    std::array<Entry, stocks::MaxSymbols> _entries{};
    stocks::StockDataProvider *_provider;
    std::string _mapping_provider;
    size_t _count=0, _selected=0, _request_index=0;
    uint32_t _generation=0, _request_generation=0, _revision=0, _gate=0;
    bool _active=false, _running=false, _gate_due=true;
    std::atomic<bool> _finished{false};
    std::string _request_symbol;
    stocks::Period _request_period=stocks::Period::FiveDays;
    stocks::Result _result;
};
