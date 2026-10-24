#pragma once
#include <array>
#include <cstdint>
#include <limits>
#include <string>

namespace stocks {
constexpr size_t MaxSymbols = 10;
constexpr size_t MaxSamples = 96;
enum class Period { FiveDays, Month, Year };
enum class Error { None, InvalidSymbol, Throttled, Authentication, Unavailable, InvalidData };
struct Sample { int64_t time = 0; double price = std::numeric_limits<double>::quiet_NaN(); };
struct Quote {
    std::string symbol, name, exchange, currency;
    double price = 0, previous_close = 0;
    bool has_previous_close = false;
    int64_t timestamp = 0;
    int delay_minutes = -1;
    std::array<Sample, MaxSamples> samples{};
    size_t count = 0;
    Period period = Period::FiveDays;
};
struct Result {
    Quote quote;
    Error error = Error::None;
    std::string message;
    uint32_t retry_after_ms = 0;
    bool ok() const { return error == Error::None; }
};
// Providers return normalized regular-session quotes and daily close
// history. Network/JSON details never escape to the UI. Lifetime must be static.
class StockDataProvider {
public:
    virtual ~StockDataProvider() = default;
    virtual const char *id() const = 0;
    virtual const char *name() const = 0;
    virtual bool supports(Period) const { return true; }
    virtual Result fetch(const std::string &symbol, Period period) = 0;
};
class YahooStockProvider : public StockDataProvider {
public:
    const char *id() const override { return "yahoo"; }
    const char *name() const override { return "Yahoo Finance"; }
    Result fetch(const std::string &, Period) override;
    static Result parse(const std::string &, const std::string &symbol, Period);
};
bool valid_symbol(const std::string &symbol);
bool parse_watchlist(const std::string &text, std::array<std::string, MaxSymbols> &symbols,
                     size_t &count, std::string &error);
std::string price_text(double price, const std::string &currency);
std::string change_text(const Quote &quote, bool percent_only = false);
const char *period_text(Period period);
}
