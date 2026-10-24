# Stock ticker app implementation plan

Status: implemented with Yahoo as the first provider and promoted to the main launcher. Hardware validation and release evaluation remain pending.
Created: 2026-10-24.

Add a stock ticker app to Gadget OS with broad international symbol coverage and, ideally, a public free data source that requires no API key registration. Prioritize a useful glanceable watchlist, clearly dated quotes, and a simple price chart. Delayed prices are a proposed acceptable compromise; real-time data is not a confirmed requirement.

Decision: start with Yahoo's chart endpoint and make the data provider replaceable from the outset. The initial experience requires no registration or API key. Yahoo's long-term reliability and permitted product use remain items to resolve before distribution; they do not prevent starting the adapter and UI implementation.

## Initial implementation

Stocks is registered on the main launcher page. The initial implementation includes a persisted ten-symbol watchlist, exact-symbol editing and ordering, price/change rows, a detail screen with 5D/1M/1Y daily-close charts, UTC quote timestamps, and cached/error states. Search and auto-rotation remain later enhancements. Quote/history caches are currently RAM-only; the watchlist persists across restarts.

Manual refresh is available by pulling down the watchlist or tapping Refresh on a detail screen. It coalesces queued requests, skips quotes fetched within 30 seconds, and retains global pacing and per-symbol retry deadlines. Requests wait for connectivity; cached data remains visible. Native tests exercise the pull gesture and retry/cooldown behavior.

`StockDataProvider` defines normalized quote/history results and supported periods. `YahooStockProvider` owns HTTP and JSON parsing; `StockService` owns background execution, refresh scheduling, and caching. Providers can be replaced through `set_provider()` using a static-lifetime adapter. A provider switch clears cached data; provider-qualified symbol lists require explicit re-saving when changing provider identity. Automatic listing translation is not implemented. UI tests use a fixture provider without Yahoo-specific UI logic.

See `include/services/StockDataProvider.h`, `src/services/StockDataProvider.cpp`, `src/services/StockService.cpp`, and `src/apps/StockTickerApp.cpp`. Run `python3 scripts/test-stocks.py` after a native build for parser, service, persistence, and UI regression checks.

## Provider findings

| Candidate | Fit with the requested experience | Decision |
| --- | --- | --- |
| Yahoo Finance chart endpoint | No key or cookie was needed in the three direct requests tested below. Yahoo covers many international exchanges, with exchange-dependent delays. The endpoint has no established public API contract in this review. Yahoo's help page states that its information must not be redistributed. | Selected as the initial provider behind a replaceable adapter. Confirm permitted product use and evaluate reliability before shipping. |
| Stooq | A possible CSV-based source to investigate, particularly for historical data. The quote request tested below returned HTTP 404, and the website presented a JavaScript verification challenge. Current anonymous access, coverage, and usage rights remain unverified. | Do not promise it as a working no-key fallback. |
| Alpha Vantage | Official API with global-equity functionality, but requires a registered key. Its support page lists a standard free allowance of 25 requests/day, with an exception for verified open-source or educational projects. | Does not meet the no-registration preference; standard quota is unsuitable for frequent multi-symbol polling. Investigate the project exception only if changing this requirement is acceptable. |
| Twelve Data | Official API requiring a key. Its stock page lists 800 free requests/day, US data and trial international symbols on Basic; broader international coverage is on paid tiers. | Potential alternative if registration or ThingPulse-funded access becomes acceptable, not a free global no-key solution. |

Sources checked on 2026-10-24: [Yahoo coverage, delays, and redistribution notice](https://help.yahoo.com/kb/account/SLN2310.html), [Stooq historical data page](https://stooq.com/db/h/), [Alpha Vantage support](https://www.alphavantage.co/support/), [Alpha Vantage API documentation](https://www.alphavantage.co/documentation/), [Twelve Data stock coverage and plans](https://twelvedata.com/stocks), [Twelve Data API documentation](https://twelvedata.com/docs). Recheck current access and terms when resuming.

## Direct endpoint probes

On 2026-10-24, four read-only HTTPS requests were made from the development machine with an identifying `ThingPulse-GadgetOS-Research/1.0` User-Agent. No API key, login, cookie, or browser impersonation was supplied.

Yahoo request pattern:

```text
https://query1.finance.yahoo.com/v8/finance/chart/{encoded_symbol}?range=5d&interval=1d
```

| Symbol | Instrument | Result |
| --- | --- | --- |
| `AAPL` | Apple, US | HTTP 200; chart metadata identified AAPL, USD, exchange NMS. |
| `NESN.SW` | Nestlé, Switzerland | HTTP 200; chart metadata identified NESN.SW, CHF, exchange EBS. |
| `7203.T` | Toyota, Japan | HTTP 200; chart metadata identified 7203.T, JPY, exchange JPX. |

The Stooq request `https://stooq.com/q/l/?s=aapl.us&f=sd2t2ohlcv&h&e=csv` returned HTTP 404. This establishes only that this request failed, not that every Stooq data endpoint is unavailable.

These are connectivity and response-shape checks, not evidence of long-term availability, universal symbol coverage, exchange-level accuracy, or permission to distribute data. Search/autocomplete, intraday charts, batching, rate limits, and direct ESP32 TLS behavior still need testing. The initial sandbox blocked DNS; the results above came from subsequent network-enabled requests.

## Proposed first version

Visual concept: [watchlist and stock detail mockup](../previews/stock-ticker-concept.png). This generated design uses the existing Gadget OS weather screen as a style reference and illustrative prices. It is not a simulator screenshot or implemented UI; final spacing and font sizes need validation at 320×480. The concept illustrates five favourites and 5D/1M/1Y period controls without changing the provisional scope below.

- A persisted watchlist of up to ten instruments, with add, remove, and reorder controls. The limit is provisional pending screen and memory testing.
- A compact list showing symbol, last price, quote currency, absolute change, and percentage change from the previous regular-session close.
- A detail card with company name, exchange, larger price, a bounded sparkline, and quote time. Swipe between favourites; optional automatic rotation can make it work as a desk ticker.
- Green/red changes with explicit plus/minus signs and neutral styling for unchanged or unavailable values. Do not rely on colour alone.
- A daily-history chart first; add an intraday view only after its availability, payload size, and request cost are verified. Label chart periods clearly.
- Distinct loading, current, delayed, cached, stale, and unavailable states. Display market-session status only when reliable provider metadata supports it.
- Preserve last-known values during outages, with their original quote timestamps. A successful network request must not make an old quote appear fresh.
- Stocks first, with ETFs supported if the provider handles them consistently. Indices, FX, crypto, holdings, portfolio valuation, news, and alerts are later scope.

Start with editable exact symbols and validation if a reliable search endpoint cannot be established. A small starter list is a convenience, not a restriction on supported symbols. Company-name search with exchange and currency disambiguation is the preferred eventual setup experience.

## International correctness

- Store provider symbol and exchange identity, not just a short ticker. The same company may have multiple listings, currencies, or ADRs; do not silently substitute them.
- Preserve suffixes and encode path/query components. Use provider-specific symbol mappings instead of assuming all APIs accept Yahoo's format.
- Validate examples across US, Switzerland, Germany, UK, Japan, Hong Kong, Canada, and Australia before claiming broad coverage. Suggested additional probes include `SAP.DE`, `VOD.L`, `0700.HK`, `SHOP.TO`, and `BHP.AX`; these were not tested in this review.
- Keep the quoted currency/unit explicit. London listings may use pence (`GBp`/`GBX`) rather than pounds (`GBP`); prevent a 100-fold display error. No automatic currency conversion in version one.
- Store timestamps in UTC and retain exchange timezone metadata. Never change the global device timezone when showing another exchange.
- Use previous regular-session close for day change, not the first point of today's chart. Keep regular, pre-market, and post-market values separate; start with regular-session quotes.
- Missing values are not zero. Validate finite prices, timestamps, currency, and previous-close denominators before computing changes.
- Use consistent adjusted/unadjusted series and label the chosen chart basis. Splits and dividends must not create misleading comparisons between incompatible data fields.
- Do not infer market status solely from weekdays and device-local time; holidays, half-days, breaks, and exchange DST differ. Show quote time when status is unknown.

## Architecture and refresh policy

Use a `StockWatchlistStore`, a background `StockService`, and an LVGL `StockTickerApp`, following existing Gadget OS persistence, network-task, and app-registration patterns. Separate provider-specific parsing from a normalized quote/history model so the source can change without rebuilding the UI.

The quote model should include instrument identity, name, exchange, currency/unit, price, previous close, quote timestamp, fetch timestamp, session and delay metadata when available, validity state, and error details. Store chart samples in bounded numeric arrays, not retained JSON documents. Cap response sizes and timeouts, and allow only one stock request in flight at a time.

### Replaceable provider contract

- Define a small `StockDataProvider` interface for instrument resolution, quote retrieval, and bounded price history. Search is optional; expose capabilities for supported history periods, search, batching, and credential requirements.
- Implement `YahooStockProvider` first. Keep Yahoo URLs, JSON field names, symbol suffix conventions, and error interpretation inside this adapter. The UI consumes normalized models only; it never sees provider JSON or sends HTTP requests.
- Let `StockService` own scheduling, cache handling, and background execution. The adapter returns normalized results and structured failures such as unsupported symbol, throttled, authentication required, and unavailable, including retry guidance when supplied.
- Persist a stable local watchlist ID with listing metadata and provider-specific symbol mappings. Changing providers preserves favourite order and labels. Resolve listings by exchange and currency as well as ticker; mark unresolved mappings for user selection rather than silently selecting another listing.
- Namespace caches by provider, listing, session, and chart basis. Discard or refresh incompatible data on a provider change; do not join histories or calculate day changes across sources.
- Keep credentials optional and specific to an adapter. Yahoo needs none; a later registered provider can add its own configuration without changing the screens or watchlist model.
- Start with one selected adapter and a simple factory/configuration point. Do not build multiple live integrations, automatic cross-provider failover, or a new backend for version one.
- Add a fixture provider for simulator screens and contract tests. Demonstrate that the same UI works with fixtures and Yahoo without provider-specific branches.

Provisional refresh policy, subject to provider limits:

- Fetch when opening the app if cached data is due; do not fetch again on every swipe or animation frame.
- Start with a five-minute refresh target while visible, stagger symbols, and add jitter. Refresh historical charts less often than quotes.
- Pause regular polling while the app is inactive. Retain RAM data and, where provider terms allow, persist occasional snapshots without writing flash on every refresh.
- Use exponential retry backoff and respect server retry guidance. Treat authentication failures, rate limiting, HTML challenge pages, and empty or malformed data as errors, not zero prices.
- Slow down when reliable session metadata says a market is closed. An old Friday close shown on a weekend is not automatically a fetch failure.

Request budgets matter: ten symbols fetched individually every five minutes for eight hours require 960 quote requests, before search or chart requests. A provider advertising a free tier may therefore still be unsuitable. Batching must be verified for the selected endpoint; do not assume it reduces per-symbol billing.

For the prototype, direct device requests avoid adding infrastructure. A ThingPulse-managed service could later preserve the no-registration experience for users while holding a provider key centrally, but it would introduce operating costs and require appropriate display/redistribution rights. A proxy does not itself grant those rights. If needed, reuse the existing Node/TypeScript deployment approach rather than introduce another technology stack; no backend is authorized or selected by this plan.

## Implementation sequence

1. Define the normalized instrument/quote/history models and `StockDataProvider` contract. Use the existing mockup as the visual direction, with clearly labeled delayed or cached data.
2. Implement `YahooStockProvider` and test the regional symbol matrix and quote/history semantics. Continue usage-term and reliability evaluation before distribution.
3. Verify direct ESP32 requests with proper certificate verification, bounded parsing, and error reporting. Test realistic Wi-Fi outages and rate-limit responses.
4. Implement a fixture provider and validate list/detail layouts for both supported displays. Decide watchlist size, chart periods, and rotation interaction.
5. Implement watchlist persistence, scheduling, and cache behavior using only the provider contract.
6. Add the LVGL app and settings, then integrate search if the source supports it reliably.
7. Run the tests below, measure resource impact, document provider limitations, and validate on hardware before release.

## Validation

- Fixtures for each supported response shape, invalid symbols, null prices, absent previous close, bad timestamps, empty series, excessive payloads, and provider errors returned with HTTP 200.
- US and international instruments, suffixes, punctuation, multiple listings, zero daily change, negative daily change, currency precision, and pence/pound handling.
- Market close, overnight sessions, DST boundaries, weekends, holidays, and chart gaps. Verify regular-session and extended-hours values are not mixed.
- Rate limiting, repeated failures, cache expiry, offline startup, recovery, watchlist edits during a fetch, and repeated app navigation.
- Unit tests for scheduling, change calculation, normalization, and bounded history handling; simulator fixtures for list/detail layouts.
- Provider contract tests, unavailable capabilities, and switching between fixture adapters with different symbol formats. Verify watchlist preservation, unresolved listing handling, and cache separation.
- Native, Color Kit Grande, and Color Kit Pro builds. Measure internal RAM, PSRAM, stack use, firmware size, network payloads, and UI responsiveness. No large finance SDK or full symbol catalogue should be required on-device.

## Resume here

Continue from the implemented Lab app: validate HTTPS requests and memory use on hardware, evaluate longer-term Yahoo reliability, and consider search, persistent quote caching, and auto-rotation as follow-ups. If Yahoo becomes unsuitable, add another adapter and explicitly resolve saved listings without rewriting the UI. Revisit distribution rights before release beyond Lab status.
