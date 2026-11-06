# Installable ISS — SDK 1.5

The `iss-tracker` app owns the pass-prediction requests, parsing, merging,
selection, refresh/backoff policy, cache format, English/German text and UI.
It is built as a freestanding ELF for ESP32 and ESP32-S3. It does not call
`IssService`, LVGL or firmware ISS functions, and does not need Gadget Companion.

## Device use

Install `iss-tracker.gapp` for the device's chip using SDK 1.5 firmware. Launch
**Installed apps → ISS**. The appearance and behaviour follow the previous
bundled ISS app: Next pass, Passes, Visible/All, selection, compass, visible-window
countdown and active-pass progress. Device location, language, timezone and
12/24-hour preferences are shared with Gadget OS.

The location button closes the app safely and opens location settings. Backing
out of settings reopens ISS with the new configuration. Closing an installed app
returns to the launcher page from which it was opened; this also applies to
Agent Usage and System Monitor.

App-enabled firmware no longer compiles or registers the bundled ISS app/service.
The old sources remain as a simulator parity reference and for legacy firmware
variants without the native app loader. The retired ISS Position demo is unrelated
and remains excluded from the catalogue.

## Data and scheduling

- Two bounded HTTPS requests fetch general and visible passes. Each accepts at
  most 20 passes and 49,152 response bytes; the app deduplicates up to 40 passes.
- Refresh is automatic every 30 minutes while open. Manual requests respect a
  one-minute minimum after completion. Failures back off, respecting numeric
  Retry-After values. The shared HTTP host also enforces its five-second spacing.
- A failed request retains the previous forecast. Location changes reject late
  results. Closing immediately prevents callback delivery into unloaded code.
- One private, versioned binary cache holds parsed predictions and their location
  key. Cache writes copy the data to a host worker; no app pointer survives there.
  The old bundled `/cache/iss.json` is not imported; first installation fetches a
  forecast before the new cache becomes available.
- The SDK caches location settings for at most 30 seconds and refreshes them on
  each app launch, avoiding filesystem reads on every countdown tick.

## Reusable SDK additions

API 1.5 appends a services table and UI methods; existing API prefixes are intact.

| API | Bound / behaviour |
| --- | --- |
| Environment | Clock readiness, network, language, time preference and shared location |
| Time formatting/parsing | Device-local display; ISO timestamps represented as unsigned epoch seconds |
| Extended HTTP | HTTPS, copied URL, one outstanding request, 49,152-byte maximum; old HTTP remains 2 KB |
| JSON document | One host-owned document, nesting ≤8; slash paths, array counts, scalar/string/number reads |
| Private cache | One atomic blob per app, maximum 16 KB; no app-supplied filesystem paths |
| UI quota | Default 64 widgets; explicit opt-in up to 256 |
| Subtree clearing | Descendant handles become invalid; no stale-handle alias on slot reuse |
| Drawing | Copied line/circle commands, ≤64 per object and ≤128 overall; no app draw callbacks |
| Loading indicator | Host-owned animated spinner, deleted with its UI subtree |
| Scrolling and text | Vertical containers, wrapped text, bounded colour/opacity styling |

The HTTP body and JSON document use PSRAM on the supported kits. HTTP workers
retain no app callback pointers. Host delivery and UI cleanup occur before ELF
unload. Settings navigation closes/reloads the app instead of leaving app-owned
callbacks alive behind a firmware screen.

## Development

```sh
.venv-build/bin/pio run -e native
python3 scripts/test-iss-installable.py
python3 scripts/test-native-ui.py
python3 experiments/native-app/build.py --app iss_tracker --chip esp32s3 \
  --toolchain "$HOME/.platformio/packages/toolchain-xtensa-esp-elf/bin"
.venv-build/bin/python scripts/install-native-app.py --port /dev/cu.usbmodem101 \
  --package .pio/native-app-packages/esp32s3/iss-tracker.gapp
```

Use `--chip esp32` for Grande. In SDL, **ISS Preview** executes the exact app C
source against the shared SDK host. `GADGET_OS_ISS_FIXTURE_DIR` can supply
`all.json` and `visible.json` responses. Location navigation uses the same settings/reopen flow in the simulator. The older **ISS** tile remains available in SDL as a visual reference.

Regression tests exercise the real app source and LVGL SDK host: next/active/
partially visible passes, 40-row scrolling content, selection, empty/missing data,
cache reload, cooldown/backoff, stale responses, localisation and 100 lifecycles.
Rendered fixtures are written to `.pio/ui-previews/iss-installable-*.png`.
`scripts/test-iss-package.py` checks both target packages with the firmware ELF
validator. Catalogue and package tests cover SDK 1.5 compatibility and retirement of the old
demos. Publishing this app requires the normal signed `apps-v*` release workflow;
no catalogue publication is performed by local builds.

## Validation — 2026-11-06

Simulator, Pro and Grande app-enabled firmware builds pass. Both ESP32 and
ESP32-S3 ISS packages pass the firmware's manifest and ELF validator. Symbol
checks confirm that neither app-enabled firmware image contains `IssService`,
`iss_app_launch` or the bundled ISS parser. Existing loader, signature, catalogue,
Agent Usage and System Monitor compatibility checks pass.

On the Pro, the user confirmed the forecast views, filtering, selection, closing
back to Installed apps and reopening after location settings. Agent Usage also
returns to Installed apps. The initial loading screen was then improved to show
an immediate host-owned spinner and explicit fetching text. A traced cold start
received and parsed 23 passes without taps in about 17 seconds, before removing
an unnecessary inter-request wait. The final build preserves the shared HTTP
rate limit. With an empty ISS cache, the user confirmed that the spinner appears
immediately and the forecast replaces it automatically without switching tabs.
The final cold-start visual check passed.

Only the Pro was flashed. No backup, restoration, commit or publication was made.
