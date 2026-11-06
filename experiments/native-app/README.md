# Experimental native app loader

Opt-in native app experiment with local browser installation; not a sandbox.
External native code has the same privileges as firmware. Only use trusted
packages. Manifest, ELF and SHA-256 checks do not authenticate executable code.

Current browser-installation results are in [INSTALLREPORT.md](INSTALLREPORT.md).
The earlier feasibility/HTTP reports describe the preceding stages.

## Agent Usage prototype

See [Agent Usage setup and testing](../../tools/agent-usage/README.md) for the
Codex/Claude allowance app, simulator preview, private companion and `.gapp`
installation steps. It is not yet published in the catalogue.

## UI and settings API 1.2

The host table keeps its API 1.0/1.1 prefix unchanged. API 1.2 appends a UI table
and per-app settings getters/setters. Existing packages remain compatible;
Agent Usage 0.3.0 declares minor version 3 (local companion support) and requires an updated host firmware.

The UI table supports opaque root/container/label/button/bar handles, pixel
bounds, row/column layout, padding/gaps, semantic styles, mutable text, progress,
visibility and selection. Styles provide the shared navy/mint theme, 14/24/32px
text, cards, tabs and back/refresh icons. `NativeUi.cpp` is used by both device
and simulator. Limits: 64 widgets including root, depth 8, 160-byte text. The
root replaces the legacy automatic column layout. UI callbacks still use the
host's closure gate; the host deletes the UI tree before unloading app code.

Settings are strings scoped to the installed app ID: up to eight keys of
24 characters (`[a-z0-9_]`), values up to 255 bytes, serialized document at most
2400 bytes. Writes replace a temporary file atomically and skip unchanged
values. Device files live under `/apps/settings/`; simulator files under
`.pio/native-app-settings/`. Updates retain settings; browser uninstall removes
them. This is API-level separation, not a security sandbox for native code.

Run `python3 scripts/test-native-ui.py` after a native build to test the actual
shared renderer and produce `.pio/ui-previews/agent-usage-*.png` screenshots.

## Architecture and scope

The existing PlatformIO/pioarduino 55.03.312-1 setup uses Arduino-ESP32 3.3.12,
ESP-IDF 5.5.5 and Xtensa GCC 14.2.0 (esp-14.2.0_20260121). The baseline resolved
LVGL version is 9.5.0. Original library ranges are untouched; experimental
profiles pin LVGL 9.5.0, ArduinoJson 7.4.3 and the baseline's LovyanGFX version
(Grande 1.2.26, Pro 1.2.19) so size comparisons don't include dependency upgrades.

Profiles: `esp32-color-kit` (classic ESP32, 8 MiB flash, PSRAM enabled) and
`esp32-color-kit-pro` (ESP32-S3 N16R2, 16 MiB flash, 2 MiB quad PSRAM).
Native SDL remains a development simulator, not a Xtensa execution backend.
There was no AGENTS.md in this checkout or its ancestor directories when reviewed.

LVGL runs synchronously in `loop()` through `lv_task_handler()`. Built-in apps
normally retain the launcher screen and return to it. This experiment defers
opening until after the handler returns, deletes the launcher, and recreates it
on exit. Launcher status widgets already unsubscribe/delete timers on deletion.
Shared services and display buffers remain running. Language-reload and OTA
popup presentation are deferred while an external app or the entire cycle batch
is active. Existing update/release-note overlays are hidden and restored after
closure; their UI allocations remain resident. Launching is blocked during
firmware download/reboot preparation.

LittleFS uses the existing partition named `spiffs`: Grande 0x510000/0x2e0000,
Pro 0xc10000/0x3e0000. Both retain dual OTA slots (2.5 MiB / 6 MiB respectively).
The filesystem label does not imply SPIFFS format. Experimental profiles specify
`board_build.filesystem=littlefs` for PlatformIO's existing uploadfs mechanism.
No partition offsets or normal profiles were changed.

## Build

From the repository root, with the project's build environment installed:

```sh
source .venv-build/bin/activate
pio run -e esp32-color-kit-native-app -e esp32-color-kit-pro-native-app

# Use the installed toolchain used by the firmware build; adjust if your
# PLATFORMIO_CORE_DIR differs from ~/.platformio.
python3 experiments/native-app/build.py --chip esp32 \
  --toolchain "$HOME/.platformio/packages/toolchain-xtensa-esp-elf/bin"
python3 experiments/native-app/build.py --chip esp32s3 \
  --toolchain "$HOME/.platformio/packages/toolchain-xtensa-esp-elf/bin"
python3 experiments/native-app/build.py --chip esp32 --app iss \
  --toolchain "$HOME/.platformio/packages/toolchain-xtensa-esp-elf/bin"
python3 experiments/native-app/build.py --chip esp32s3 --app iss \
  --toolchain "$HOME/.platformio/packages/toolchain-xtensa-esp-elf/bin"
python3 scripts/test-native-app.py
python3 scripts/test-native-app-cycles.py
python3 scripts/test-native-install.py
```

Installable packages are `.pio/native-app-packages/<chip>/{native-demo,native-iss}.gapp`.
Each includes its manifest, 40×40 RGB565 icon and executable. Intermediate
`manifest.json` / `app.elf` files remain available in each app's subdirectory.
They are separate build outputs, never firmware byte arrays. Use `--id my-app`
to build a separately identified sample, or `--version 0.2.0` to build an update.
The matching chip is mandatory: `esp32` for Grande, `esp32s3` for Pro.

`build.py` reports sizes, SHA-256 and compiler identity. The stripped ELF's
removed `.dynamic` section can cause a readelf diagnostic; the loader uses section
headers, not the program's dynamic segment. The linker RWX-segment warning does
not imply a memory protection boundary exists on these devices.

`GADGET_OS_NATIVE_APP_LOADER` is undefined/false by default. All first-party
experiment translation units are preprocessor-empty then; upstream loader
sources and the SDK include path are added only by the two experimental profiles.
Normal environments remain buildable:

```sh
pio run -e esp32-color-kit -e esp32-color-kit-pro -e native
```

After building enabled and disabled targets, audit the artifacts:

```sh
python3 experiments/native-app/audit-builds.py \
  --toolchain "$HOME/.platformio/packages/toolchain-xtensa-esp-elf/bin"
```

## Install, update and remove in the browser

Flash the experimental firmware once for the matching board:

```sh
pio run -e esp32-color-kit-native-app -t upload --upload-port /dev/cu.YOUR_PORT
# Pro: replace the environment with esp32-color-kit-pro-native-app.
```

1. Connect the device and browser to the same trusted local network.
2. Open **Labs → Apps** and scan the QR code. It pairs the browser with a local
   installer on port 8081; the link expires after ten minutes without API use.
3. Choose the matching `.gapp` file and select **Install / Update**. The page
   displays installed names and versions and offers **Update** and **Remove**.
4. Choose **Back to launcher** on the device. Installed apps appear automatically
   under **Labs**, using their packaged icons. More icons can be reached by scrolling.
5. Open **ISS Position** or **Native Counter**, then use the app's **Close** button
   to return to the launcher. Close an app before updating or removing it.

No filesystem image upload, device backup or restoration is needed. Settings
and caches are preserved. Earlier loose `/apps/native-demo` and `/apps/native-iss`
files are left alone but are no longer discovered; install their `.gapp` versions.
Installed packages persist across restart in `/apps/installed/<id>.gapp`.

Uploads are streamed to a temporary file, then the full manifest, target chip,
API version, ELF structure and SHA-256 digest are checked. Only a successful
validation permits an atomic LittleFS rename over the previous package. Failed,
interrupted or rejected uploads retain the installed version. There is no
runtime rollback if otherwise valid native code crashes or fails to initialize.

Up to eight apps are supported, with a 70,836-byte per-package limit and space
reserved for staging. Installing a new app requires no host firmware rebuild or
hardcoded launcher registration. This is a local developer workflow, not a
public app store: packages are unsigned and execute with firmware privileges.

### Package format (version 1)

All integers are unsigned little-endian. The 52-byte header contains:

| Offset | Bytes | Meaning |
| --- | --- | --- |
| 0 | 8 | Magic `GOSAPP1\0` |
| 8 | 4 | JSON manifest length, padded with spaces to a multiple of four, max 2048 |
| 12 | 4 | Icon length, exactly 3200 bytes |
| 16 | 4 | ELF length, max 65536 bytes |
| 20 | 32 | SHA-256 of header bytes 0–19 followed by the complete payload |

The payload is the padded JSON manifest, then a 40×40 RGB565 little-endian icon,
then the ELF. No trailing bytes are allowed. IDs match `[a-z][a-z0-9-]{0,31}`;
name and version are printable ASCII, 1–32 characters. The manifest executable
field remains `app.elf` as a format identifier, not an arbitrary filesystem path.
The ELF entry is four-byte aligned within the package.

The pairing key is carried in the URL fragment, removed from the address bar
and kept in browser session storage. API calls send `X-App-Key`; the device
checks Host/Origin and does not allow CORS. The page uses plain local HTTP, so
pair only on a trusted network. See the implementation and install test report
for the experimental limits; these checks do not sandbox or sign native code.

## ABI and ownership

`experiments/native-app/sdk/gadget_app.h` is the complete C-compatible boundary.
The explicit ELF entry `gadget_app_entry(int,char**)` receives two opaque pointers
in argv: a read-only host table and a writable lifecycle descriptor. They are not
strings. Both tables contain size/major/minor fields. API 1.1 appends HTTP and
numeric JSON helpers; API 1.0 counter packages remain supported.
The manifest rejects wrong chip, missing fields, path traversal and unsupported
versions. A 2 KiB manifest / 64 KiB ELF limit bounds file-buffer allocations.

Host wrappers expose logging, one screen, labels, buttons, label updates,
periodic timers and deferred closure. Handles are opaque and session-local.
Maximum 16 direct widgets and four timers, labels up to 160 bytes, timer periods
20–60000 ms. Button captions are children owned by the host's object tree.

No LVGL, Arduino, IDF, STL or exceptions cross this ABI. Apps may use C++ only
with C linkage for the entry, no exceptions/RTTI and no dynamic global/static
initializers, destructors, TLS, tasks, ISRs or background callbacks. No runtime
constructor processing occurs. Explicit initialization/shutdown must own all
state. This example is freestanding C and has no runtime/library dependency.
Compiler-generated helpers must be implemented within the app or rejected by
`--no-undefined`; arbitrary host symbol lookup is intentionally unavailable.

The linker binds symbols locally, merges GOT data into `.data`, and aligns text
size to four bytes. The latter avoids rounded text copies overlapping read-only
data on classic ESP32's word-access instruction RAM. Preflight checks section
bounds, entry, loaded-section overlap, symbol strings and relative relocation
addresses/values before passing a buffer to the length-less upstream loader API.
Only the subset generated by this SDK is accepted. This is defensive parsing,
not a security verifier for hostile native machine code.

All app callbacks pass through host-owned trampolines and a delivery gate.
A close request marks the gate closed immediately, preventing new delivery.
Shutdown is processed only after LVGL and all app callback frames have returned.
Timers and widgets are destroyed before shutdown/unloading; shutdown can log but
cannot create more UI/timers. The registered shutdown function is also called
on partial initialization failure. The executable and file buffer are freed on
failure, with a host-owned error screen and a Back button. Repeated open/close
is allowed only from the launcher; serial commands cannot interrupt built-in apps.

## Device validation

Connect the device, wait until boot completes and return to the launcher. Close
other serial monitors. Using a Python environment with pyserial installed:

```sh
python3 experiments/native-app/cycles.py --port /dev/cu.YOUR_PORT --log /tmp/grande-native-cycles.log
# Repeat on Pro with a separate log.
python3 experiments/native-app/cycles.py --analyse --log /tmp/grande-native-cycles.log
```

The firmware command is `native test 100` followed by newline. Each cycle opens
the filesystem package, runs timer callbacks, sends actual LVGL reset/close
button events, and restores the launcher. It waits 500 ms between cycles.
`native open` and `native close` are also available. Closing while a timer/button
callback is active cannot unload that stack frame. Leave Wi-Fi/network services
in a consistent state for comparisons. Test with Wi-Fi both enabled and disabled
if background network allocations obscure the trend. Do not pull the sleep switch.

The runner drains startup output for eight seconds after opening the serial
port (`--boot-wait` overrides this), so a classic ESP32 UART reset does not
discard the test command before `Serial.begin()`. It captures logs without
deliberately asserting reset lines, checks 100 complete cycles, callback activity
and 100 reset/close app acknowledgements, detects common reset/crash markers,
and compares free/largest block medians and regression slopes after five warm-up
cycles. Default tolerance is 1024 bytes, adjustable with `--tolerance`.
On the tested macOS/S3 connection, an initial USB reset was observed despite
the DTR/RTS settings. Boot output before cycle 1 is excluded; a reset after the
first cycle starts fails the test.
This is a trend heuristic; inspect raw logs as well. Use touch manually, then
open Calculator and verify it still functions after the test.

For invalid-package testing, stage/upload each variant separately: missing
manifest, `{` malformed JSON, chip changed to the other target, `api_major: 2`,
`api_minor: 3`, missing ELF, truncated ELF. Each must show a useful error and
return via Back without reboot. Restore the valid package afterwards. The
portable regression tests cover these parsers, truncations, 40,000 mutations,
nested callback-close gating and 100 desktop dynamic-library demo ABI cycles.
They do NOT validate Xtensa execution, real LVGL ownership, or physical display.

## Measurements

Serial records cover launcher idle, launcher released, relocation with the file
buffer still live, executable loaded after freeing that buffer, app UI created,
activity, shutdown/unloaded and launcher restored. They include:

- Internal 8-bit heap free/largest, executable-capable free/largest, PSRAM free/largest.
- LVGL pool total/free/largest. Pro uses the built-in 64 KiB LVGL pool; Grande
  uses the libc allocator, where these LVGL monitor values can be zero. The pool
  is already part of resident memory, not another independent heap to sum.
- Boot-wide internal heap low-water metric, separately labelled from a per-cycle
  *sampled* internal minimum. Sampling does not capture every transient peak.
- Requested text section size and pointers, actual data allocation size and PSRAM flags.
  Executable allocation cost is measured through heap deltas: querying the size
  of its IRAM-alias pointer asserts in this ESP-IDF version.
- Load duration (including release/read/validation/init), unload duration, and
  combined unload/launcher-restoration duration in microseconds.

These heap capabilities overlap: executable memory may share internal physical
RAM, and generic 8-bit allocations may land in PSRAM. Never sum them as separate
RAM pools. Classic ESP32 code must use internal executable RAM. This initial S3
experiment also uses internal executable RAM, not upstream's optional PSRAM
execution mode. Both retain resident OS/display/services. Dynamic loading adds
RAM-resident executable storage and file/relocation overhead; RAM savings are
not assumed. See REPORT.md for measured build sizes and outstanding hardware work.

## OTA and follow-up

Core OTA currently writes an app slot, leaving LittleFS untouched when the
partition layout stays unchanged. The loader rejects incompatible manifests before load; compatibility across core releases
has NOT been tested, and filesystem upload/formatting/partition migration can
remove packages. This experiment does not alter the OTA workflow.

## Independent ISS Position app (API 1.1)

Install the matching `native-iss.gapp` through **Labs → Apps**, using the browser
workflow above. No filesystem image replacement is needed.
Open **Labs → ISS Position** (satellite icon). The separately built app owns
the endpoint, response fields, formatting, Refresh/Close actions and age label.
It displays latitude, longitude, altitude and speed from
[Where the ISS at?](https://wheretheiss.at/w/developer). The built-in ISS pass
forecast app and its Pollux Labs API are unchanged. This sample does not include a map.

The host HTTP wrapper runs on a host-owned 8 KiB FreeRTOS worker stack. It copies
the URL (maximum 255 bytes), permits HTTPS without redirects, uses the built-in
CA bundle, and caps the response at 2048 bytes. At most one request is outstanding
across all sessions, with at least five seconds between starts. Connection/read
timeouts are five seconds, TLS handshake timeout eight seconds, and body writes
stop after a 20-second budget. These are transport/inactivity limits, not a hard
wall-clock deadline for arbitrary DNS/header behaviour. A slow or failing request
does not block the UI; memory remains owned by the host until it finishes.

Closing clears callback/user pointers immediately and requests cooperative
transport cancellation. The worker holds only copied data and a generation
number, never an app callback or an app-owned pointer. It publishes completion
with release/acquire synchronization after destroying transport objects. The UI
discards results for closed or older sessions, then frees the result. Unloading
does not wait for the network. A newly opened app may show “Network busy” until
the previous worker finishes; Refresh retries. The response pointer is borrowed
only during its UI-thread callback. JSON helpers accept finite top-level numeric
fields, return rounded scaled integers and reject overflow/missing/wrong types.

The app fetches on launch and on Refresh, not periodically. It prevents duplicate
requests, waits five seconds between attempts, and waits a minute after HTTP 429.
It shows offline, transport and invalid-response errors. Close works while loading.
The SDK remains a trusted-code experiment, without task/process isolation.

Serial commands `native iss`, `native iss refresh`, and `native iss close` open
the app and deliver actual LVGL button events. `native close` is a host recovery
command. `scripts/test-native-app.py` additionally validates both ISS ELFs and
runs 100 desktop close-pending/unload/reopen cycles with late completions under
ASan/UBSan, plus success, invalid-response, offline, API-version and rate-limit
checks. These simulate transport; actual HTTPS/TLS and Xtensa execution require
the device tests recorded separately in REPORT.md.

With the host and packages installed, run real-network lifecycle tests:

```sh
python3 experiments/native-app/http-cycles.py --port /dev/cu.YOUR_PORT \
  --log /tmp/native-http-cycles.log
```

This runs three successful-fetch/close-during-refresh/reopen/retry sequences.
It requires working Wi-Fi and the public API. A response that wins the close
race is reported as a test failure to repeat, not claimed as a cancellation pass.
The [HTTP follow-up report](HTTP-REPORT.md) separates hardware observations from
desktop simulations and historical counter results.

### Local companion (API 1.3)

Agent Usage 0.3.0 defaults to the paired local companion, without an embedded URL.
See [Gadget Companion](../../tools/gadget-companion/README.md) for setup.
`companion_read(json, capacity)` is appended after the 1.2 settings functions;
it returns a bounded, age-adjusted JSON snapshot or -1 when absent. The host only
exposes usage summaries to the canonical `agent-usage` app ID. This is a host API
policy, not a security sandbox for native code. No callbacks or credentials cross
this boundary. Older API 1.0–1.2 binaries keep their existing table prefix.

### Optional hardware diagnostics

Native-app heap/lifecycle logging is off by default. Send `native trace on`
over USB while actively reading serial to enable lifecycle, raw touch edges,
input polling gaps and SDK button events; send `native trace off` to disable it.
`native test 100` enables diagnostics automatically. Pro USB writes are
nonblocking: diagnostic output may be dropped if the host stops consuming it,
rather than freezing the UI for the HWCDC backpressure timeout.

Agent Usage 0.4.0 adds a Codex detail view within the existing 64-widget SDK limit.
It uses optional companion snapshot fields for seven daily counts, lifetime/peak
counts, streaks and longest turn. The ABI remains 1.3; use updated host firmware
and companion 0.2.0 to forward these fields. Missing details do not block quotas.

Agent Usage 0.5.0 shows remaining allowance only on the overview. Codex details
select Today / 7 days / calendar Month in UTC. Companion 0.3.0 sends bounded
period sums and coverage (`cx_p0..2_{v,u,n,e}`), plus seven five-day month groups
(`cx_m0..6_{v,u,bar}`). Missing buckets are not zero-filled. The host allowlists
these optional fields within the existing 2048-byte snapshot and SDK 1.3 limits.

## System Monitor and SDK 1.4

[System Monitor 0.1.0](../../docs/system-monitor.md) uses the existing companion
pairing, a dedicated allowlisted system snapshot, string decoding, opacity and
bounded 60-point line charts. The SDK additions are append-only; existing 1.3
Agent Usage binaries remain supported. Installer and catalogue compatibility
advertise minor version 4. Build with `--app system_monitor` for either chip.
