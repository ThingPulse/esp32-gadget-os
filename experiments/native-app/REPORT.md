# Native app loader feasibility report

Date: 2026-11-04. Recommendation: **Inconclusive pending hardware testing**.

The minimal loader has run successfully on a physical Color Kit Pro (ESP32-S3,
2 MiB PSRAM), including 100 consecutive load/reset/close cycles. Both chip
builds pass, but no classic ESP32 device was available. Thus the evidence does
**not** yet establish viability on both chips or an S3-only limitation. Early
physical taps were also inconsistent and remain an unresolved usability issue.

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

Final S3 run: **100/100 cycles**, 100 Reset and 100 Close acknowledgements,
7–8 app callbacks per cycle, no detected crash/reset during the cycles. The
initial USB connection produced `USB_UART_CHIP_RESET` before cycle 1; the
analyser excludes this startup event, not resets after testing starts.

The user confirmed Calculator works after closure. Physical Reset and Close
callbacks were captured; Reset also leaves a visible acknowledgement. However,
the user reported that initial short taps missed and holding longer helped,
after which response improved. This has **not** been explained or fixed. The
user also found an OTA notice overlaying the first counter version: the final
firmware suspends existing update overlays and defers new ones across the whole
cycle batch, restoring notices afterwards. It refuses launch during installation.

The final external app revision was installed by replacing only its filesystem
package, with the core firmware unchanged. No app logic is embedded as bytes.

| Criterion | ESP32 | ESP32-S3 |
|---|---|---|
| Build enabled and disabled | Pass | Pass |
| Filesystem ELF, timer, Reset and Close execute | Not tested | Pass |
| 100 cycles without reboot or continuing memory decline | Not tested | Pass within stated threshold |
| Invalid packages recover to launcher | Not tested | Six variants, two attempts each, pass |
| Existing Calculator after closure | Not tested | User confirmed |
| Consistently responsive physical taps | Not tested | Initial missed taps unresolved |

The six device rejection cases were wrong chip, unsupported API major,
malformed manifest, missing manifest, missing ELF and truncated ELF. Each was
opened and closed twice in one boot, reporting the expected error and returning
to the launcher through the serial close command without a reset between attempts. A deliberate reflash/reset
separated different fixtures. [Rejection trace](results/pro-invalid.log).

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
Classic ESP32 uses the libc LVGL allocator, so its runtime behavior must be
measured separately. Heap capability totals overlap and must not be summed.

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

## Remaining risks and next decision

Classic ESP32's scarce word-access executable RAM remains the main untested
feasibility risk. S3 also uses internal executable RAM here; upstream PSRAM
execution/cache synchronization is not enabled or evaluated. Neither a desktop
ABI test nor a successful cross-build proves classic ESP32 instruction execution.

The native ABI is deliberately narrow and compiler-specific. No sandbox,
constructor processing, arbitrary tasks, crash containment or signatures exist.
Malformed-data checks cannot make untrusted machine code safe. Small bounded
parser allocations and LVGL allocation behavior remain subject to system-wide
memory exhaustion; executable/file-buffer allocation failures are handled explicitly.

Next: repeat the hardware tests on Grande and investigate the initial missed
physical taps. Overall recommendation remains **inconclusive pending hardware
testing**, with basic S3 feasibility demonstrated. Asynchronous HTTP is deferred;
callback cancellation for pending network requests, core-OTA package preservation
and ABI compatibility across firmware versions remain untested.
