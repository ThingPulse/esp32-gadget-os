# Native app loader feasibility report

Date: 2026-11-04. Recommendation: **Viable on both chips**, for the tested minimal lifecycle.

Physical Color Kit Grande (classic ESP32-D0WD-V3, 8 MiB flash) and Color Kit
Pro (ESP32-S3, 2 MiB PSRAM) each completed 100 consecutive load/reset/close
cycles. Both chip builds pass and the user confirmed Calculator after closure
on both. This establishes feasibility for the tiny synchronous SDK/demo, not a
production community-app platform or adequate headroom for much larger apps.
Initial physical taps on Pro were inconsistent and remain a usability issue;
the Grande manual checks passed without that reported problem.

## Implemented

- Separate experimental branch and two opt-in firmware profiles; defaults unchanged.
- Pinned Espressif loader 1.3.4, C API 1.0, target-specific counter packages.
- Deferred UI-thread open/close, launcher teardown/recreation, host-owned widgets,
  callbacks and timers, partial-initialization cleanup and host-owned error UI.
- Bounded manifest/ELF preflight; no arbitrary host symbol exports.
- Memory/timing logs and a serial 100-cycle runner/analyser.
- Exact build, whole-filesystem upload and manual acceptance instructions in README.md.

## Build measurements

Final build figures are recorded below after validation. Static RAM is the
PlatformIO report, not runtime free heap; binary sizes include image overhead.

| Target | Normal flash usage | Experimental flash usage | Increase | Static RAM increase | Firmware .bin increase |
|---|---:|---:|---:|---:|---:|
| esp32-color-kit | 2,252,407 B | 2,280,099 B | 27,692 B | 536 B | 28,784 B |
| esp32-color-kit-pro | 2,233,075 B | 2,247,635 B | 14,560 B | 520 B | 15,568 B |

All five final builds passed: normal native SDL, normal Grande/Pro, and both
experimental targets. Normal firmware flash/static RAM figures match the
pre-experiment baseline exactly. Symbol audits confirm no `native_app::` or
`esp_elf_` symbols in either disabled firmware. Enabled images contain both.
Both target-specific LittleFS package images built successfully. The S3 package
was installed in a copy of the device filesystem preserving its existing files.

External package sizes: ESP32 **1,902 B**, ESP32-S3 **1,904 B**, each including
a **1,772 B** executable and its manifest. Firmware `.bin` sizes: Grande
normal **2,307,440 B**, experimental **2,336,224 B**; Pro normal **2,289,280 B**,
experimental **2,304,848 B**. The larger Grande increase includes UART serial
command support that the baseline had not initialized.


The external demo contains 564 bytes of aligned `.text`, 177 bytes of `.rodata`,
4 bytes of `.data` and 16 bytes of `.bss` on both targets. This particular demo
uses the common instruction subset; it was separately compiled using each chip's
compiler wrapper. That does not establish arbitrary cross-chip binary compatibility.

The upstream loader requests 564 executable bytes and 206 data bytes including
alignment slack. S3 reported 212 usable bytes for the data allocation, with code
and data both in internal memory. The executable pointer is an IRAM alias:
`heap_caps_get_allocated_size()` asserted on it during the first hardware attempt.
That instrumentation bug was removed before the successful run; code cost is
reported through heap deltas, not an unsupported pointer-size query.

## Automated validation

- Manifest acceptance/rejection: valid target, malformed/missing fields, wrong
  target, unsupported major/minor versions, path traversal and oversized JSON.
- Both generated ELF files pass the firmware's exact preflight implementation.
  Every truncation and targeted bad-header cases reject; 40,000 deterministic
  byte mutations complete under AddressSanitizer and UndefinedBehaviorSanitizer.
- Nested delivery-gate tests prove close blocks new callbacks and cannot permit
  unloading until the final in-flight callback returns.
- 100 desktop dlopen/init/timer/reset/close/shutdown/dlclose cycles of the actual
  demo source pass under sanitizers. This uses a small test host function table;
  it does not execute the embedded loader or prove LVGL resource cleanup.
- Synthetic cycle-log fixtures verify stable/leaking/incomplete/crashed/missing-
  callback/error cases. Synthetic data is NOT used as hardware evidence.

Commands: `python3 scripts/test-native-app.py` and
`python3 scripts/test-native-app-cycles.py`.

## Hardware results

Final runs on **each board: 100/100 cycles**, 100 Reset and 100 Close acknowledgements,
7–8 app callbacks per cycle, no detected crash/reset during the cycles. The
Pro’s initial USB connection produced `USB_UART_CHIP_RESET` before cycle 1; the
analyser excludes this startup event, not resets after testing starts.

The user confirmed Calculator works after closure on both boards. Physical Reset and Close
callbacks were captured; Reset also leaves a visible acknowledgement. However,
the user reported on Pro that initial short taps missed and holding longer helped,
after which response improved. This has **not** been explained or fixed. The
user also found an OTA notice overlaying the first counter version: the final
firmware suspends existing update overlays and defers new ones across the whole
cycle batch, restoring notices afterwards. It refuses launch during installation.

The final external app revision was installed by replacing only its filesystem
package, with the core firmware unchanged. No app logic is embedded as bytes.

| Criterion | ESP32 | ESP32-S3 |
|---|---|---|
| Build enabled and disabled | Pass | Pass |
| Filesystem ELF, timer, Reset and Close execute | Pass | Pass |
| 100 cycles without reboot or continuing memory decline | Pass within stated threshold | Pass within stated threshold |
| Invalid packages recover to launcher | Six variants, two attempts each, pass | Six variants, two attempts each, pass |
| Existing Calculator after closure | User confirmed | User confirmed |
| Consistently responsive physical taps | Manual checks passed | Initial missed taps unresolved |

On each board, the six device rejection cases were wrong chip, unsupported API major,
malformed manifest, missing manifest, missing ELF and truncated ELF. Each was
opened and closed twice in one boot, reporting the expected error and returning
to the launcher through the serial close command without a reset between attempts. A deliberate reflash/reset
separated different fixtures. Rejection traces: [Pro](results/pro-invalid.log),
[Grande](results/grande-invalid.log).

### S3 memory and timing

Medians over cycles 6–100; bytes, except timing. These are instrumented CPU-side
lifecycle durations, not time until the first fully rendered frame.

| Measurement | Result |
|---|---:|
| Load: launcher release, read, validate, relocate, UI creation | 27.05 ms median; 28.76 ms p95 |
| Unload alone | 1.51 ms median; 1.60 ms p95 |
| Unload plus launcher recreation | 81.64 ms median; 84.13 ms p95 |
| Launcher internal free / largest block | 142,352 / 98,292 B |
| Executable-capable free at launcher / during activity | 110,332 / 109,648 B |
| Internal free with ELF file buffer still live | 139,716 B |
| Internal free after releasing file buffer | 141,512 B |
| Internal free during activity / after shutdown | 141,668 / 142,508 B |
| Median internal bytes recovered during unloading | 840 B |
| PSRAM free / largest block | 2,069,904 / 2,031,604 B |
| LVGL pool free before / after launcher release | 39,808 / 56,252 B |
| LVGL pool free after app UI creation | 54,612 B |
| Boot-wide minimum internal free | 113,604 B |
| Per-cycle sampled minimum internal free, median | 139,716 B |

The 840 B recovery includes loader/host resource effects; it is not an isolated
measurement of the executable allocator's overhead. Freeing the ELF input buffer
recovered 1,796 B. Launcher deletion recovered 16,444 B (about 16 KiB) of **LVGL pool
capacity**, not system heap: the Pro's configured 64 KiB pool remains reserved.
Classic ESP32 uses the libc LVGL allocator; its separate measurements appear
below. Heap capability totals overlap and must not be summed.

Comparing the first and last ten post-warm-up launcher-return samples: internal
free and largest block medians changed by **0 B**; executable-capable medians
also changed by **0 B**. PSRAM free increased 320 B. LVGL free decreased just
8 B and its largest block median was unchanged; free-space slope was
−0.12 B/cycle. No continuing decline above the 1,024 B heuristic threshold was
found. Immediate launcher-return and later idle samples differ because the next
LVGL housekeeping/layout pass has not yet run; compare like phases.

This is one short run with ordinary background networking enabled. Sampled
minima are not true transient peaks, and the tiny demo does not establish memory
headroom for large community apps.

Evidence: [cycle trace](results/pro-cycles.log), [analysis](results/pro-analysis.txt),
[phase/timing measurements](results/pro-measurements.json),
[manual interaction trace](results/pro-manual.log),
[build/symbol audit](results/build-audit.json).
Only experiment messages and the initial reset marker are retained; device
settings and full flash images are not committed. The full original 16 MiB
flash backup was restored after testing, including firmware, filesystem, NVS
and OTA metadata; esptool verified the written hash and rebooted the Pro.

### Grande measurements and comparison

Grande also passed **100/100 cycles**, including all 100 Reset and Close
acknowledgements. Its UART discarded an immediate command during startup in the
first manual attempt; the runner now drains startup output for eight seconds
before sending the command. The final run uses that fix. The serial monitor
remained connected throughout the manual counter/Calculator check.

Medians over cycles 6–100:

| Measurement | Grande / ESP32 | Pro / ESP32-S3 |
|---|---:|---:|
| Load through UI construction | 113.46 ms | 27.05 ms |
| Unload alone | 1.78 ms | 1.51 ms |
| Unload plus launcher recreation | 149.68 ms | 81.64 ms |
| Launcher-idle internal free | 70,156 B | 142,352 B |
| Largest executable-capable block at launcher | 49,140 B | 98,292 B |
| Internal free after launcher release | 91,200 B | 142,352 B |
| Internal free after executable loading | 90,972 B | 141,512 B |
| Internal free during activity | 89,508 B | 141,668 B |
| PSRAM free | 4,185,972 B | 2,069,904 B |

**Timing includes serial logging.** Grande uses 115200-baud UART and Pro uses
USB Serial/JTAG. Their logging overhead differs, so this is not evidence that
the actual loader CPU work is four times faster on S3. Each board also retained
its own settings and background services; this is not a controlled hardware
benchmark. Grande load p95 was
114.04 ms, unload p95 1.79 ms and unload/recreate p95 150.72 ms.

Grande’s launcher deletion recovered a paired median **21,020 B of internal
heap**, unlike Pro’s fixed-pool capacity recovery. Loading the executable after
freeing the file buffer consumed paired medians of 228 B internal 8-bit memory
and 808 B executable-capable memory; these capabilities overlap. Code and data
were internal, with 224 B reported usable data allocation. The input buffer
release recovered 1,796 B. These phase deltas include host/allocator effects.
The boot-wide minimum was 43,504 B; the median final per-cycle sampled minimum
was 69,444 B. Zero LVGL monitor values reflect the libc allocator, not zero usage.

First versus last ten post-warm-up return samples: internal free improved 224 B,
executable-capable free improved 196 B, both largest blocks were unchanged,
and PSRAM free/largest were unchanged. No continuing decline exceeded the
1,024 B threshold. The smaller executable block is a real capacity constraint;
a 564-byte code example does not establish feasibility for 50 KiB or larger code.

The extra free memory while the app runs comes from **launcher teardown**. The
same lifecycle strategy could be used by built-ins. Native loading itself adds
RAM-resident code/data and file-buffer overhead; it is not inherently a RAM saving.

Grande evidence: [cycle trace](results/grande-cycles.log),
[analysis](results/grande-analysis.txt),
[phase/timing measurements](results/grande-measurements.json),
[manual interaction trace](results/grande-manual.log).
The original 8 MiB flash was backed up in checksum-verified chunks, then verified
as one complete image. This was necessary after high-speed serial corruption;
a cable change and slower retries were used. The test image preserved all
20 original filesystem files. After testing, the entire original 8 MiB flash
was restored, including firmware, filesystem, NVS and OTA metadata. Esptool
verified the written hash and rebooted the Grande.

## Remaining risks and next decision

**Basic feasibility is demonstrated on both chips.** Both use internal executable
RAM here; optional S3 PSRAM execution/cache synchronization remains untested.
The native ABI is deliberately narrow and compiler-specific. No sandbox,
constructor processing, arbitrary tasks, crash containment or signatures exist.
Malformed-data checks cannot make untrusted machine code safe. Small bounded
parser allocations and LVGL allocation behavior remain subject to system-wide
memory exhaustion; executable/file-buffer allocation failures are handled explicitly.

Next: investigate Pro’s initial missed taps, test larger realistic apps and
measure timing with logging disabled before making performance claims.
Asynchronous HTTP is deferred; callback cancellation for pending network
requests, core-OTA package preservation and ABI compatibility across firmware
versions remain untested. The recommendation is **viable on both chips** for
this measured, single-foreground-app experiment, with these limits retained.
