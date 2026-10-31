# Color Kit Pro display investigation

Hardware measurements were collected on a connected Color Kit Pro on 2026-10-31.
The benchmark compares the baseline and candidate driver paths within the same
firmware. Parser-test fixtures remain synthetic and are separate from hardware logs.

## Measured result

[Full hardware results and raw measurements](results/2026-10-31-pro-display.md)
contain all three repetitions, build versions, and the benchmark firmware hash.

| Workload/metric | Baseline: 8-row PSRAM | Candidate: 64-row bulk DMA |
|---|---:|---:|
| Complete full redraw, mean | 152.508 ms | 51.715 ms |
| Complete full redraw, P95 | 174.414 ms | 60.564 ms |
| Partial update, mean | 10.308 ms | 6.022 ms |
| Raw full-screen write, mean | 10.929 ms | 6.985 ms |

The candidate reduces full-redraw duration by **66.1%** (2.95× throughput for this
workload). Increasing the buffer to 32 rows with the original writer already
reduces it to 66.477 ms; merely moving eight rows into internal RAM has no material
benefit. The major finding is the cost of rendering many small strips, rather
than an insufficient bus clock. With the 64-row candidate, rendering and other
refresh work still dominate: the writer occupies 7.874 ms of the 51.715 ms redraw.

These are repeatable sliding-card/text redraws, not measured launcher-swipe FPS.
Networking was off. The user subsequently confirmed substantially faster real-app
home-screen swipes, while reporting tearing that was also present before the
change and on the Grande. No claim of 60 FPS or tear-free presentation is made.

## Findings in the current driver

Both boards use a 320 × 480 ILI9488 panel configured for a 40 MHz write clock.
The Pro uses the ESP32-S3's 16-bit parallel bus; the Grande uses SPI. The installed
LovyanGFX ILI9488 driver selects 16-bit pixels for parallel and three-byte pixels
for SPI. Bus width alone does not establish application frame rate.

The Pro's baseline LVGL buffer is eight rows in PSRAM: 5,120 bytes, requiring 60
LVGL flushes for a full portrait-screen redraw. The Grande already uses 32 rows
in internal RAM. The common flush callback calls `writePixels` and `endWrite`
synchronously. On the Pro, RGB565 byte conversion goes through LovyanGFX's
256-byte staging buffers. The bus already uses DMA internally; the experiment
avoids those small conversion transfers rather than introducing DMA from scratch.

The existing serial `frame_ms` statistic starts at the first flush, so it omits
rendering the first strip. Millisecond resolution also obscures short transfers.
It should not be used alone to compare these buffer sizes. The standalone
benchmark times each complete synchronous `lv_refr_now` call in microseconds.

Relevant implementation: `include/ColorKitPro.h`, `include/ColorKitGrande.h`,
`src/user_code.cpp`, and the installed LovyanGFX `Panel_LCD::writePixels`,
`LGFXBase::create_pc_fast`, and ESP32-S3 `Bus_Parallel16::writePixels/writeBytes`.

## Opt-in change

`esp32-color-kit-pro-display-test` builds the normal application with:

- A 64-row internal DMA-capable buffer (40,960 bytes), selected after hardware measurements.
- One bulk RGB565 byte swap followed by `writePixelsDMA` using `swap565_t`.
- A synchronous `endWrite` before notifying LVGL that the buffer is reusable.
- A 32-row internal fallback, then the original eight-row path if internal allocations fail.

The standard Pro target remains unchanged; the faster path is available in the opt-in application target for visual and full-application validation.
The bus clock, pin mapping, panel initialization, and refresh period are unchanged.
The transfer helper is shared by the application experiment and the benchmark.

This does not overlap rendering with transfer. A future double-buffer experiment
needs genuinely asynchronous completion and correct buffer ownership; merely
adding a second buffer to a blocking callback does not provide that overlap.
See [LVGL's display setup documentation](https://lvgl.io/docs/open/9.5/main-modules/display/setup).

## Repeatable measurements

The standalone `esp32-color-kit-pro-display-benchmark` target starts no network
services and does not load or save application settings. It uses the existing Pro
partition configuration. Building it does not flash a device. Follow the main
README's partition-migration precautions before any USB upload to an older device.

```sh
.venv-build/bin/pio run -e esp32-color-kit-pro-display-benchmark
# Once the intended Pro is connected, replace PORT with its USB serial port:
.venv-build/bin/pio run -e esp32-color-kit-pro-display-benchmark -t upload --upload-port PORT
.venv-build/bin/pio device monitor -e esp32-color-kit-pro-display-benchmark --port PORT | tee /tmp/pro-display.log
```

Capture one complete run. If startup was missed, start a fresh log and send `r`
over the serial monitor to repeat. The firmware waits five seconds at boot before
starting. Do not combine multiple runs in one input file.

It compares five profiles within the same firmware:

| Profile | Memory | Rows | Transfer |
|---|---|---:|---|
| baseline-8-psram | PSRAM | 8 | Original writer |
| internal-8 | Internal DMA-capable | 8 | Original writer |
| internal-32 | Internal DMA-capable | 32 | Original writer |
| dma-32 | Internal DMA-capable | 32 | Bulk swap + DMA |
| dma-64 | Internal DMA-capable | 64 | Bulk swap + DMA |

Each profile runs three workloads: raw full-screen transfers, full LVGL redraws
of sliding cards and text, and partial updates of a moving small badge. Each has
10 warmup frames and 90 measured frames, repeated three times with profile order
rotated. The raw test includes clearing the strip buffer in its total time;
`io_mean_us` isolates the writer itself, including conversion and transfer waits.
Per-frame yield and serial reports are outside timing. Allocation failures are
reported as skips, never silently replaced with a different profile.

```sh
python3 scripts/summarize-display-benchmark.py /tmp/pro-display.log
```

The summary reports median-of-run mean and P95 refresh duration, transfer time,
flush count, pixel count, and percentage time change from baseline. Negative
change means faster. These are work-duration measurements, **not** panel scan
rate, tear-free presentation, or sustained application FPS. The fixed workload
isolates the driver; it does not represent Wi-Fi contention or every application.

Before adopting the change:

1. Check the final red/green/blue swatches, text, and moving cards for wrong colours,
   corruption, and tearing. Timing alone cannot establish correct display output.
2. Compare identical workloads and pixel counts, including P95 and maximum frame
   times in the raw log. Check allocation and internal-memory records.
3. Restore the full application using the `esp32-color-kit-pro-display-test` target,
   then test page swipes, scrolling, rotation, and networking. Check that boot logs
   say `internal-bulk-dma`, rather than `baseline-fallback`.
4. Use the ordinary `esp32-color-kit-pro` target to restore the baseline application.

Do not raise the bus clock as the first experiment. If transfer time becomes a
small fraction of complete redraw time, further gains require profiling rendering
and main-loop services rather than faster bus writes.
