# Tide app implementation plan

Status: proposed; implementation has not started.
Created: 2026-10-24.

Add a tide clock to Gadget OS, inspired by the customer's [ThingPulse tide clock](https://github.com/ThingPulse/esp32-tide-clock). Preserve its useful tide dial, countdowns, and sun/moon information while integrating with Gadget OS services and supporting configurable locations.

The app appears feasible on the existing hardware. The main decisions are geographical coverage and the data provider. The proposed scope below is a starting point, not an approved final specification.

## Source review

Reviewed upstream revision: `c84f04ebd98d53a36269428d1034a2ff7c0865b7`.
The review covered source code and provider documentation; the upstream firmware was not built or run.

- The project is a standalone TFT_eSPI application with its own Wi-Fi, time synchronization, update loop, fonts, and filesystem artwork. Rebuild its display in LVGL and use Gadget OS services.
- Configuration targets Claiborne, station `8572468`, using Baltimore reference station `8574680`. It applies high/low time corrections of -146/-148 minutes and height multipliers of 0.96/1.09. These values must not become defaults for other locations. Prefer predictions requested directly for the selected station where supported; verify this against the provider before implementation.
- NOAA requests retrieve high/low predictions for yesterday through tomorrow, refreshed every six hours. These are predictions, not measured water levels.
- The dial follows elapsed time between predicted extrema rather than assuming a fixed tidal period. It does not represent actual water height or current speed.
- Local timestamp conversion uses `mktime()` with `tm_isdst` left at zero, which can introduce daylight-saving errors. Store UTC internally and format times for the selected location.
- Tide calculations assume that previous and next events exist, with no adequate guard against missing events or a zero cycle duration. Empty prediction arrays can also underflow the unsigned sorting bound.
- Fetching is synchronous and the refresh path contains delays. A failed fetch still leads into calculations and the normal refresh schedule. Move networking off the UI thread and implement bounded timeouts, retries, and explicit validity states.
- Tide units and height labels are fixed to feet. Support metres and feet and retain the vertical reference datum.
- The project uses 60 tide JPEG frames plus moon bitmaps and additional fonts. Prefer an LVGL dial and existing moon rendering to avoid importing an asset bundle and duplicate libraries. Check both application flash and filesystem costs independently.
- The source is MIT licensed. Preserve applicable copyright and licence notices when reusing code.

References: [main implementation](https://github.com/ThingPulse/esp32-tide-clock/blob/c84f04ebd98d53a36269428d1034a2ff7c0865b7/src/main.cpp), [settings](https://github.com/ThingPulse/esp32-tide-clock/blob/c84f04ebd98d53a36269428d1034a2ff7c0865b7/src/settings.h), [licence](https://github.com/ThingPulse/esp32-tide-clock/blob/c84f04ebd98d53a36269428d1034a2ff7c0865b7/LICENSE).

## Proposed first version

- Up to five saved tide locations, with one active location and swipe navigation.
- Main screen showing location, tide dial, rising/falling state, next high and low times and heights, and countdown to the next event.
- A seven-day high/low event list with dates and daily tidal range.
- Sunrise/sunset and moon phase as secondary information, calculated for the tide location rather than an unrelated active weather location.
- Location-local time, metric/imperial units, clear next-day labels, and accessible station/provider/datum details.
- Persistent prediction caching so valid forecasts remain useful without connectivity. Distinguish cached valid predictions from expired or unavailable predictions.
- An optional 24–48 hour curve with a now marker and daylight shading only where the provider supplies intermediate height predictions. Otherwise use an event timeline.

The photograph's large clock can be retained as a design influence, but the location and next tide event should receive more emphasis. Produce a mockup before finalizing the screen layout.

## Data provider decision

NOAA is suitable for an initial release limited to its supported stations. Coverage includes US coasts and various Caribbean and Pacific islands, not worldwide coverage. Its API supports direct device requests without an API key.

NOAA harmonic stations support intermediate prediction heights. Subordinate stations support high/low predictions only and require MLLW as the datum. Do not present an interpolated curve as a provider-supplied height prediction.

For worldwide coverage, evaluate a provider such as WorldTides. It supports coordinate-based requests, heights, extrema, and timezone information, but requires an API key and credits. Its documented restrictions on sharing responses between users must be considered before designing a shared backend cache. Confirm costs, attribution, coverage, and commercial terms before selecting it.

Keep the firmware data model independent of the provider. A NOAA-only first version does not inherently require a new backend. Decide whether a backend is needed after selecting the provider and credential model.

References: [NOAA coverage](https://oceanservice.noaa.gov/facts/tideprediction.html), [NOAA data API](https://api.tidesandcurrents.noaa.gov/api/prod/), [NOAA metadata API](https://api.tidesandcurrents.noaa.gov/mdapi/prod/), [WorldTides API](https://www.worldtides.info/apidocs).

## Multiple locations

Use the existing five-location weather interaction as a model, while storing tide favourites separately. A tide location needs provider/station identity, coordinates, timezone, display name, datum, and supported prediction capabilities.

Allow friendly aliases such as Home beach, but keep the actual prediction station visible in details. Search should offer supported nearby stations for explicit selection; do not assume that the closest station across a headland or estuary represents the user's beach. Reusing coordinates from a weather favourite can be a convenience, not an automatic station assignment.

Each favourite should retain its own cache and timezone. Switching locations must not alter the global device timezone or affect other apps.

## Implementation outline

1. Resolve geographical scope and provider, then verify representative station responses, including subordinate stations and locations with irregular tide patterns.
2. Define a compact prediction model: UTC event time, high/low type, height, units/datum, source, coverage interval, and fetch time. Represent interval-height samples separately when supported.
3. Add a `TideLocationStore` following existing persistence conventions and a `TideService` using existing background network patterns. Keep provider parsing separate from the UI.
4. Add bounded response parsing, cache persistence, retries with backoff, and explicit loading/valid/stale/error states. Reject malformed or incomplete updates without destroying a usable cache. Stop countdown/dial calculations once required bracketing events are unavailable.
5. Build `TideApp` with existing fonts, LVGL primitives, and reusable moon rendering. Register it using the current app architecture and integrate configuration with existing settings conventions.
6. Add forecast/event navigation and location management. Keep secondary information readable without crowding the primary tide display.
7. Validate the behavior below, build supported targets, measure memory/flash changes, and test on hardware before release.

## Validation

- Fixture tests for valid responses, provider errors, empty arrays, malformed fields, missing previous/next events, and oversized responses.
- Correct countdowns at exact tide events, midnight, year boundaries, daylight-saving transitions, and across location timezones.
- Locations with unequal cycles or fewer events; do not assume exactly two highs and two lows every calendar day.
- Correct metre/foot conversion and datum labeling; clear distinction between predictions and observations.
- Offline startup with and without cache, failed refresh, cache expiry, recovery, and location changes during a fetch.
- A responsive UI during network failures and no effects on other apps' timezone or connectivity.
- Firmware builds and flash/RAM checks for Color Kit Grande and Color Kit Grande Pro, plus simulator layout checks and hardware rendering checks.

Expected runtime cost is modest for cached events and a simple dial. Actual flash, RAM, and rendering impact must be measured; avoid adding upstream graphics libraries and font bundles wholesale.

## Possible later additions

- Measured water level compared with prediction where observations are available.
- Wind, water temperature, and waves/swell, depending on provider coverage and user needs.
- Tidal current and slack-water forecasts as separate data products. Do not infer slack water from high/low tide times; the relationship varies by location. See [NOAA's explanation](https://www.tidesandcurrents.noaa.gov/faq.html).
- User-defined height windows and a richer planning view once reliable interval-height data is available.

## Resume here

First decide whether the launch must support worldwide locations or may begin with NOAA coverage. Then confirm the provider and station-selection experience, create the UI mockup, and implement the data/cache layer before the display. Recheck upstream changes and provider terms when resuming.
