# Experimental native app loader

Opt-in feasibility experiment, not an installable app platform or a sandbox.
External native code has the same privileges as firmware. Only use trusted
packages. Neither the manifest nor ELF checks authenticate executable code.

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
python3 scripts/test-native-app.py
python3 scripts/test-native-app-cycles.py
```

Packages are `.pio/native-app-packages/<chip>/native-demo/{manifest.json,app.elf}`.
They are separate build outputs, never firmware byte arrays. `build.py` reports
executable/package sizes, SHA-256 and compiler identity. The stripped ELF's
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

## Upload

Build/upload the firmware for the matching board:

```sh
pio run -e esp32-color-kit-native-app -t upload --upload-port /dev/cu.YOUR_PORT
# Pro: replace the environment with esp32-color-kit-pro-native-app.
```

There was no existing incremental arbitrary-file upload service. PlatformIO
`uploadfs` writes the WHOLE LittleFS partition: it replaces filesystem settings
and caches, but not NVS. Use a development device and back up anything needed.
Do not use it expecting to preserve existing filesystem contents. Stage a clean
filesystem image outside the normal `data/` tree, optionally adding your backed-up
files to that staging directory first:

```sh
mkdir -p .pio/native-app-fs/esp32/apps
cp -R .pio/native-app-packages/esp32/native-demo .pio/native-app-fs/esp32/apps/
PLATFORMIO_DATA_DIR="$PWD/.pio/native-app-fs/esp32" \
  pio run -e esp32-color-kit-native-app -t uploadfs --upload-port /dev/cu.YOUR_PORT

mkdir -p .pio/native-app-fs/esp32s3/apps
cp -R .pio/native-app-packages/esp32s3/native-demo .pio/native-app-fs/esp32s3/apps/
PLATFORMIO_DATA_DIR="$PWD/.pio/native-app-fs/esp32s3" \
  pio run -e esp32-color-kit-pro-native-app -t uploadfs --upload-port /dev/cu.YOUR_PORT
```

At runtime only `/apps/native-demo/manifest.json` and `/apps/native-demo/app.elf`
are read. Open **Labs → Native demo**. Watch the counter, reset it, then close.
Reset leaves a visible “Reset received” acknowledgement; reset/close callbacks
also emit explicit serial messages.
The launcher is rebuilt without a reboot. Open Calculator and use it afterwards.

## ABI and ownership

`experiments/native-app/sdk/gadget_app.h` is the complete C-compatible boundary.
The explicit ELF entry `gadget_app_entry(int,char**)` receives two opaque pointers
in argv: a read-only host table and a writable lifecycle descriptor. They are not
strings. Both tables contain size/major/minor fields. API 1.0 is supported.
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
`api_minor: 1`, missing ELF, truncated ELF. Each must show a useful error and
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
partition layout stays unchanged. Future loaders could preserve packages and
reject incompatible manifests before load; compatibility across core releases
has NOT been tested, and filesystem upload/formatting/partition migration can
remove packages. This experiment does not alter the OTA workflow.

Asynchronous HTTP/JSON is deferred until the basic loader and lifecycle pass on
hardware. No HTTP callbacks into app code exist in this implementation.
