# System Monitor 0.3.0

Implements the approved [v1 contract](system-monitor-v1-contract.md) as an installable
SDK 1.11 app. Per-core CPU load requires the updated firmware snapshot forwarder
and Gadget Companion 0.7.1. Initial
collector support is macOS, without admin access or additional Node packages.

## Run

```sh
node tools/gadget-companion/src/cli.mjs
```

Use the existing computer pairing. The same process supplies Agent Usage and
System Monitor. Codex collection is asynchronous; its latency does not block
system sampling. To monitor a Mac without Codex installed:

```sh
node tools/gadget-companion/src/cli.mjs --no-codex
```

Open Installed apps → System Monitor. The header's Back closes the app; bottom
tabs select Overview, Processes and Storage. Processes sorts by CPU or memory.
Tap the CPU card in Overview to see individual logical-core loads: eight cores
per page, up to 64 cores. Each bar spans 0–100% of that core, independently of
the aggregate CPU percentage. Back returns to Overview. An older companion
shows “Per-core data unavailable”; first samples and gaps show “--”.
Refresh requests a sample on the next companion poll, including refreshing the
slower cached metrics. Taps never wait synchronously for network operations.

## Metric definitions

- CPU: differences between `os.cpus()` cumulative counters, expressed as a
  percentage of total machine capacity. First observation and gaps over six
  seconds are unavailable, not fabricated zeros.
- Process CPU: differences between `ps` cumulative CPU time, divided by elapsed
  monotonic time and logical CPU count. This intentionally differs from Activity
  Monitor's per-core percentage convention. RSS is resident memory, not total
  virtual size or macOS's memory-footprint metric. Sort ties use PID. Collection
  uses `comm`, not command-line arguments; only the basename/PID/counter is sent.
- RAM: `(anonymous + wired + compressor-occupied - purgeable) × page size`,
  bounded by physical RAM. File-backed cache and compressed pages' uncompressed
  logical size are excluded. This is a physical-memory estimate, not memory
  pressure. Displayed MB/GB/TB memory and storage units use binary scaling.
  System Monitor 0.2.1 and the updated companion retain tenths of a percent.
  Its memory graph shows a labeled adaptive range (with one percentage point
  of padding) so small changes are visible; the CPU graph stays at 0–100%.
  Older companions still work, with whole-percent memory readings.
- Network: delta bytes on the default route's `enN` interface. Unknown/tunnel
  interfaces show unavailable rather than double-counting underlying traffic.
  Rates use decimal B/s, KB/s, MB/s. Counter/interface resets invalidate one sample.
- Storage: writable startup/data volume via `df -k /System/Volumes/Data`. Usage
  is capacity minus available, accounting for space unavailable to this volume
  within its shared APFS container. Finder's purgeable-space estimate can differ.
- Battery: `pmset -g batt`. Distinguish charging, plugged in, on battery and no
  battery. Uptime comes from the operating system.

Fast samples target two-second intervals. Battery, route, volume name and disk
capacity are cached for at most 30 seconds. Commands time out after 1.2 seconds;
individual failures yield unavailable metrics. Sampling never overlaps. Two
minutes of CPU/RAM history are RAM-only; missed intervals insert chart gaps.

## Connection and privacy

The existing authenticated companion endpoint adds `POST /v1/system`; the status
response advertises support and a refresh counter. System snapshots remain
bounded to 2048 bytes. The firmware allowlists fields, strips unknown data and
adds elapsed observation age on reads. Only the canonical `system-monitor` app
can call the new System Monitor API. Forgetting the computer clears both caches.

After 10 seconds without a new sample, readings are muted and explicitly marked
Offline / Last seen. No data shows Waiting for computer. Missing pairing points
to Add apps → Pair computer. Existing trusted-LAN plain-HTTP limitations apply.
No process arguments, credentials, prompts, persistent process history or cloud
reporting are added. Companion files supplied explicitly with `--system-snapshot`
are a developer opt-in, and contain process names.

## Simulator and tests

Native simulator: open System Monitor Preview for clearly documented sample data.
For a live simulator file:

```sh
node tools/gadget-companion/src/cli.mjs --system-snapshot .pio/system-live.json
GADGET_SYSTEM_SNAPSHOT=.pio/system-live.json .pio/build/native/program
```

Build packages with `experiments/native-app/build.py --app system_monitor
--version 0.1.0 --chip esp32s3 --toolchain <toolchain-bin>` (or `--chip esp32`).
Install using the existing `scripts/install-native-app.py` command. Unsigned
local packages require Developer mode; nothing here publishes a catalogue.

Validation commands:

```sh
npm test --prefix tools/gadget-companion
python3 scripts/test-companion.py
python3 scripts/test-system-monitor.py
python3 scripts/test-native-ui.py
```

SDK 1.4 appends a copied, fixed-scale 60-point chart API, opacity and ellipsis
styles, a bounded string reader and isolated system snapshot/refresh calls.
Old ABI prefixes and the 64-widget limit are preserved. The app uses no floating
point runtime, no direct LVGL objects and no device-side background tasks.

## Validation — 2026-11-06

Native simulator, Grande and Pro firmware builds passed. Automated checks cover
collection/parsing, full-history payload bounds, missing and zero values, offline
and reconnect states, both process sorts, SDK compatibility, 100 app lifecycles,
UI bounds, large values, existing Agent Usage, package loading and signing.
Actual simulator renders are saved in `docs/previews/system-monitor-*-implemented.png`.

System Monitor 0.1.0 and SDK 1.4 firmware were installed on the Pro. Companion
0.4.0 sends live Mac statistics and Codex usage using the existing pairing. The
user confirmed that all views, sorting, Refresh and Back fit and respond normally,
and that Agent Usage continues to work. Offline/reconnect rendering was tested
automatically; a physical disconnect observation was not requested from the user.
The Grande build was compiled, but not flashed for this milestone. No release or
catalogue was published, and no device backup or restoration was performed.

The optional schema-1 `cpu_cores` field contains up to 64 two-digit lowercase
hex values in `os.cpus()` order: `00`–`64` are 0–100% and `ff` is unavailable.
The existing 2 KiB snapshot limit and aggregate CPU history remain unchanged.
Hosts with more than 64 logical cores are explicitly labeled as showing the
first 64; aggregate load still covers the entire host.
