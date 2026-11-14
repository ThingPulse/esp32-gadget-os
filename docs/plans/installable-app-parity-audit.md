# Migrated app feature audit

Audit date: 2026-11-14. Scope: Stocks, Planes, Transit and Home Assistant.
This supplements the migration plan; the earlier phase-3 completion note was
not sufficient evidence of feature parity. Hardware runtime and live-provider
checks remain release gates, separate from compile and fixture tests.

## Method and references

Compared the retained bundled screens, their callbacks, providers, refresh and
cache lifecycles, settings, parsing/validation, error paths and cleanup against
the actual C sources used by both the installable packages and native adapters.
The shared host was included wherever the package delegates a former service.

| App | Bundled implementation reviewed | Package / shared implementation |
| --- | --- | --- |
| Planes | `src/apps/PlanesApp.cpp`, `src/ui/NearbyPlanesView.cpp`, `include/ui/NearbyPlanesView.h`, `src/services/PlanesService.cpp`, `src/services/AirportCache.cpp`, `include/ui/PlaneAltitudeColor.h`, Planes forms in `src/apps/SettingsApp.cpp` | `experiments/native-app/example/planes.c`, `planes_cache.h`, `NativeServices.cpp` migration bridge |
| Transit | `src/apps/TransitApp.cpp`, `src/services/TransitService.cpp`, `src/services/TransitProvider.cpp` and their model headers | `experiments/native-app/example/transit.c` |
| Stocks | `src/apps/StockTickerApp.cpp`, `src/services/StockService.cpp`, `src/services/StockDataProvider.cpp`, `include/ui/StockChart.h` | `experiments/native-app/example/stocks.c`, `NativeUi.cpp`, shared `ChartShade.h` |
| Home Assistant | `src/apps/HomeAssistantApp.cpp`, `HomeAssistantModel.cpp`, `HomeAssistantService.cpp`, `HomeAssistantDiscovery.cpp`, `HomeAssistantHttp.cpp`, `HomeAssistantSetup.cpp`, `HomeAssistantEntityStream.cpp`, setup HTML | `experiments/native-app/example/home_assistant.c`, shared setup-page asset, `NativeSetup.cpp`, `NativeHttp.cpp` |
| Shared lifecycle/settings | Old pause/resume, configuration save, settings navigation and teardown paths | `NativeSettings.cpp`, `NativeServices.cpp`, `NativeApp.cpp`, native adapters, package/catalogue compatibility |

## Planes

| Feature / old behavior | Finding and resulting behavior |
| --- | --- |
| Nearby radar is **always airborne-only** | Regression fixed: both the old app's search request and its renderer excluded ground targets, regardless of the general preference. Package parsing now excludes ground targets for ADS-B and OpenSky too. Settings explain this; the legacy preference remains stored. |
| Nearest 20 valid targets, sorted by distance | Retained; ground targets cannot displace airborne targets in the quota. Coordinates and finite numbers are validated. |
| Four-provider fallback; reject unusable HTTP 200 | Retained: adsb.lol, airplanes.live, adsb.fi, OpenSky, with unit conversions and bounded search areas. |
| Compass rings, altitude bands, heading silhouettes, unknown heading, north-up / user marker | Restored; landmark, trail and aircraft layers have distinct drawing order. |
| Identity selection, previous/next, nearest fallback | Retained. Tapping an aircraft selects it; the summary opens details. |
| Trail histories collected for **every** current aircraft | Regression fixed: 20 independent histories, each with 20 samples, protect present aircraft before reclaiming departed slots. Selecting another aircraft immediately shows its existing history. |
| Ignore duplicate positions; update altitude at unchanged location | Restored; stationary samples no longer consume history. |
| Zoom clips trails to the radar circle | Restored quadratic segment clipping; zoom retains histories, changing center clears them. Old samples are not pinned to the rim or discarded just because an endpoint is outside it. |
| Faded, altitude-colored selected trail | Restored, with two-pixel lines and a bounded draw budget. Only the selected trail was drawn in the old UI; that remains the behavior. |
| Airport landmarks learned from route lookups | Regression fixed: route lookup is no longer selected-aircraft-only. Selected target gets priority, then other nearby callsigns, within three lookups per position cycle. |
| Route cache and negative caching | Restored callsign-normalized cache, six-hour successes and thirty-minute misses; changing selection does not erase routes. |
| Up to 64 airport markers; avoid overlapping labels | Restored from the migration's smaller 24-entry cache and twelve-marker draw cap. All markers fit within widget/draw quotas; overlapping labels are suppressed. |
| Airport persistence and view scoping | Retained package persistence; added one-time migration of `/cache/plane-airports.json`. Valid matching views survive the upgrade, original file remains untouched, and a marker prevents re-import after uninstall. Earlier 24-entry package snapshots remain readable. Shared-device coordinates are resolved before validating the cache so reopening does not clear matching airports. |
| Direction, approach/pass/recede, airline/type fallback, route summary | Restored. Unknown type and generic aircraft fallbacks no longer become empty labels. |
| Detail target remains fixed when aircraft disappears | Fixed: details retain their own ICAO identity rather than silently switching to the nearest replacement. The missing-target message and Close remain available. |
| Registration, ICAO, squawk, altitude/speed/distance/direction, route airport names | Restored ICAO and cached-data failure notice; all fields retained, with vertical speed/country/military information additionally available. Airport municipality is used when name is absent. Periodic refresh preserves detail scroll. |
| Update age, loading/error status | Restored age and cached/failure status; connectivity and progress indicators are present. |
| Units, range, coordinate validation, IP guess | Retained metric/aviation units, presets and custom range, manual coordinates and IP guess. Draft save/discard, shared location and asynchronous HTTPS lookup are migration improvements. |

## Transit

| Feature / old behavior | Finding and resulting behavior |
| --- | --- |
| Eight favourites, stop search, duplicate detection, save failure rollback | Retained. Choosing an already-saved search result selects it; adding a new stop opens its board as before. |
| Search form and results, country/area, new search, empty/error states | Restored layouts; Latin display names use the same glyph folding as the original. |
| Choose, swipe or previous/next stop | Retained. Removing a favourite requires confirmation; cancel leaves saved stops intact. |
| Compact route badge, destination, countdown, absolute time, platform, live indicator | Restored. Black route colors remain valid rather than becoming the fallback color. Empty line names fall back to Transit. |
| Cancellation flags at trip, departure and place level | Retained; cancelled departures do not advertise a delay. |
| Both late and early live predictions | Regression fixed: signed delays are shown in board metadata and the detail hero. |
| Details even when no trip ID is supplied | Fixed: the departure hero still opens, with a clear no-route-details message. |
| Forty route calls; fallback arrival/scheduled times; deduplicate by stop identity/time | Retained, including stop-ID deduplication restored from the migration's name-based shortcut. |
| Skip past trip portion; timeline; boarding/departure context | Retained. Departed countdown restored; device-timezone clock rendering remains host-owned. |
| Preserve board when returning from details/settings | Regression fixed: return to the same stop keeps the successful snapshot, including offline; switching stops clears only that board. |
| Cached/error/live distinction | Fixed: real board errors persist through navigation; informational cooldown notices do not turn fresh data into “cached”. |
| Two-minute refresh, 30-second manual cooldown, Retry-After/backoff | Retained; now explicit cooldown feedback, Wi-Fi/clock gating, and ten-second countdown repaint in both board and details. |
| Attribution / coverage / timezone / public-service terms | Restored source-information screen. |
| Late replies and stop/configuration changes | Generation checks retained; stale replies cannot populate another stop or search. |

## Stocks

| Feature / old behavior | Finding and resulting behavior |
| --- | --- |
| Ten ordered symbols, validation, uppercase normalization, duplicates, empty list | Retained, including 24-byte symbol alphabet and multi-line keyboard Enter. |
| Editor examples and visible caret | Examples restored; shared keyboard focus/cursor fix retained. |
| Company name in watchlist | Restored; cached status takes its place only for failed/stale quotes, as before. |
| Absolute **and** percentage price change in details | Restored. Unchanged quotes use neutral color and signed zero; JPY formatting and additional small-crypto precision remain. |
| 5D / 1M / 1Y daily closes, missing observations, flat chart | Retained. Restored monotone smoothing, gaps, shaded area and endpoint marker through the shared chart renderer. |
| Quote time UTC, exchange, prior close, delayed/cached/error status | Retained; previous close still excludes the quote's exchange-local day and never uses range-start `chartPreviousClose` as yesterday's close. |
| Reordering preserves existing quotes and retry deadlines | Regression fixed: symbols are matched by identity rather than resetting every quote on Save. |
| Close and reopen retains usable quotes | Regression fixed: the old resident service retained quotes in RAM. A bounded package snapshot now preserves quotes and retry timing across unload/reopen, with wall-clock age validation. |
| Refresh all / selected, pull refresh, swipe between stocks | Retained. Connectivity/progress indicators restored. |
| Five-minute refresh and selected-symbol priority | Retained; other due symbols continue refreshing while detail is open. |
| Global provider backoff and per-symbol errors | Restored provider-wide failure/Retry-After gate so other symbols do not immediately bypass a provider outage or limit. Invalid symbols remain isolated. |
| Late replies after editing or closing | Generation/lifecycle fencing retained, including reordered lists. |

## Home Assistant

| Feature / old behavior | Finding and resulting behavior |
| --- | --- |
| Four pages, seven tiles, ordered pages/entities, aliases/icons | Retained; page position and empty-page help restored. |
| Light/switch toggle, scene/script activation, sensor/binary sensor details | Retained; unavailable actions rejected and controls visibly disabled. |
| Connected / connecting / disconnected state | Restored in dashboard and `/api/status`; the migrated setup API had always reported `connected:false`. |
| Stable dashboard widgets while polling | Restored in-place values/colors/pending indicators; polling no longer recreates the tile tree. |
| On/off tint, pending spinner, expected toggle/brightness state | Restored, with timeout notices and confirmation polling rather than treating HTTP acceptance as a confirmed light state. |
| Scrolling long tile names and readable sensor details | Restored marquee labels and wrapped, larger detail values. |
| Brightness and warmth controls | Restored numeric percent/Kelvin labels, drag feedback, expected pending slider value, disabled states, and preserved detail scroll. |
| Color palette and details Done | Retained all ten colors; Color heading and Done restored. |
| Five-second configured-entity polling, missing/unavailable states, stale connection | Retained, including duplicate-entity state propagation across pages and thirty-second freshness. |
| Auth/TLS/redirect handling; custom CA | Retained in bounded host HTTP. API 1.11 restores the former 256 KiB device response bound and supports nesting through 32 levels. |
| Paired setup QR/link, same-network instructions, idle expiry, stop on close | Retained via shared setup server. Original browser setup page is reused, rather than reimplementing its page/tile editing. |
| Search/filter/hide-unavailable/25-item pagination/manual IDs | Retained with parameterized discovery template; restored duplicate-result and boolean validation. Administrator-token failures keep the manual-ID fallback. |
| Private config, token retention on same-origin edits, no token in config reads | Retained; settings are app-private and migration leaves original configuration untouched. |

## Explicit architectural/presentation differences

These must not be mistaken for untested claims of byte-for-byte equivalence:

- The package radar has the same 20 aircraft / 20 history points / 64 airport
  bounds. Its in-memory route cache holds 64 recent callsigns rather than the
  old service's 512; evicted routes are fetched again. This stays within the
  loader's 128 KiB loaded-image bound. Position requests cannot be starved by
  route enrichment, and host request spacing can spread lookups across cycles.
- Settings use explicit staged Save/Discard rather than the old unit switch's
  immediate autosave. Shared-device location is additional functionality.
  The old ground switch did not affect the nearby radar; the package explains
  the actual airborne-only behavior instead of presenting an ineffective switch.
- Packages unload their workers/state on close. Stocks uses private persistent
  snapshots to retain its old reopen/offline behavior; transient Home Assistant
  state is fetched again and actions never continue after the app closes.
- Touch controls are SDK widgets, not the identical LVGL object tree. Radar range
  uses a button menu; chart inputs remain 96 normalized samples; progress uses
  a spinner instead of the old moving header line. All corresponding actions
  and information remain accessible. Exact typography/geometry is not asserted.
- Simulator tutorial-only providers are replaced by explicit fixture adapters;
  synthetic tutorial data is not used for normal device/provider operation.

## Compatibility and validation

The four updated packages require **API 1.11 firmware**. The ABI table prefix is
unchanged; 1.11 adds presentation style values and increases the bounded extended
HTTP/JSON allowance. Catalogue filtering, package validation, loader acceptance,
browser-store metadata and native adapters advertise the same version. Earlier
packages remain supported.

Package versions: Planes 0.3.0, Transit 0.3.0, Stocks 0.2.0,
Home Assistant 0.2.0. Local developer artifacts: `.pio/parity-apps/{esp32,esp32s3}`.

Tests cover the package sources, not just retained legacy code:

- `test-phase3-installable.py`: location drafts/stale IP replies; airport migration;
  ground exclusion; independent and deduplicated trails; zoom/clipping; route
  selection/cache; missing aircraft; Transit cache return, early predictions and
  missing-trip details; stable Home Assistant widgets, connection API, action
  confirmation, paired setup/expiry and repeated lifecycle/settings transitions.
- `test-stocks-installable.py`: parser/history/previous-close checks, prices and
  changes, reordering/reopen cache, multiline caret/Enter, global Retry-After,
  late responses and installed-app settings/uninstall behavior.
- `test-native-ui.py`: new disabled/marquee/chart presets, missing-data chart gaps,
  bounds/quotas, handles and repeated cleanup; existing installable dashboard and
  system-monitor screens remain covered.
- `test-native-app.py`, `test-native-install.py`, `test-native-catalog.py`: both-chip
  package/ELF validation, corruption checks, ABI compatibility and catalogue gates.
- Native screenshots are under `.pio/ui-previews`; inspect Planes radar/trail,
  Transit board/search/details, Stocks watchlist/chart/editor, Home dashboard and
  light details as part of release review.

Native and both device firmware builds and the above regression suites are the
compile/fixture verification gate. Physical touch behavior, real network/TLS
providers, memory/lifecycle testing on hardware, signed publication and flashing
still require release validation. No device flashing or publication is performed
by this audit.

Verification results on 2026-11-14: all six regression scripts listed above
passed, including the shared-location airport-cache reopen and details-scroll
checks. All four packages built for ESP32 and ESP32-S3. Native screenshot review
covered the restored radar/trail, airport marker, Transit board and Home Assistant
dashboard; source-level fixtures do not establish live-provider or device parity.
Native, `esp32-color-kit` (Grande) and `esp32-color-kit-pro` firmware builds
also passed on the final sources. `git diff --check` passed.
