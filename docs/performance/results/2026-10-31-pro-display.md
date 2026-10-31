# Color Kit Pro hardware measurements — 2026-10-31

ESP32-S3 revision 0.2, 2 MiB PSRAM, 320 × 480 portrait, 40 MHz parallel bus.
Arduino-ESP32 3.3.12 / ESP-IDF 5.5.5, LovyanGFX 1.2.36, LVGL 9.6.0; debug build.
Network disabled in the standalone benchmark. Three repeats of 90 measured frames
per profile/workload, with 10 warmup frames. Profiles share the same firmware.

Benchmark firmware SHA-256: `38af092e74481426750422c58b2795ee43e942e74ecc7455a09fbeea183f138f`.

[Raw measurements](2026-10-31-pro-display.log)

| Scene | Profile | Runs | Mean refresh ms | P95 ms | Transfer ms | Flushes | Pixels | Time change |
|---|---|---:|---:|---:|---:|---:|---:|---:|
| full-redraw | baseline-8-psram | 3 | 152.508 | 174.414 | 18.027 | 60.0 | 153600 | +0.0% |
| full-redraw | dma-32 | 3 | 63.002 | 73.270 | 8.738 | 15.0 | 153600 | -58.7% |
| full-redraw | dma-64 | 3 | 51.715 | 60.564 | 7.874 | 8.0 | 153600 | -66.1% |
| full-redraw | internal-32 | 3 | 66.477 | 76.668 | 12.324 | 15.0 | 153600 | -56.4% |
| full-redraw | internal-8 | 3 | 152.508 | 174.414 | 18.027 | 60.0 | 153600 | +0.0% |
| partial-update | baseline-8-psram | 3 | 10.308 | 12.212 | 0.780 | 3.1 | 5612 | +0.0% |
| partial-update | dma-32 | 3 | 6.022 | 7.762 | 0.390 | 1.1 | 5612 | -41.6% |
| partial-update | dma-64 | 3 | 6.022 | 7.762 | 0.390 | 1.1 | 5612 | -41.6% |
| partial-update | internal-32 | 3 | 6.160 | 7.947 | 0.525 | 1.1 | 5612 | -40.2% |
| partial-update | internal-8 | 3 | 10.308 | 12.212 | 0.780 | 3.1 | 5612 | +0.0% |
| raw-full | baseline-8-psram | 3 | 10.929 | 10.929 | 10.817 | 60.0 | 153600 | +0.0% |
| raw-full | dma-32 | 3 | 6.999 | 6.999 | 6.953 | 15.0 | 153600 | -36.0% |
| raw-full | dma-64 | 3 | 6.985 | 6.985 | 6.922 | 8.0 | 153600 | -36.1% |
| raw-full | internal-32 | 3 | 10.527 | 10.527 | 10.479 | 15.0 | 153600 | -3.7% |
| raw-full | internal-8 | 3 | 10.929 | 10.929 | 10.817 | 60.0 | 153600 | +0.0% |

Median of repeated runs; negative time change means faster than the baseline.
Refresh time is measured work, not the LCD scan rate or application FPS.


## Full application check

The opt-in application was flashed into the existing active app slot after the
benchmark. Boot completed and restored the home route. Boot telemetry confirmed:

```text
draw buffer rows=64 bytes=40960 memory=internal-bulk-dma
internal RAM free=101960 largest=57332
```

The partition table matched the project before testing. Only the app slot was
flashed; the bootloader, partition table, NVS, and filesystem were not written by
the upload. The original app slot was backed up locally before testing.

Opt-in application SHA-256: `1ee449c53d0f2942b6fee6133288dcb1aa1734b1d2883b6020036e540b1b26d4`.

User feedback after the full-app trial: home-screen swipes feel substantially
faster. The user still observes tearing/slightly shifted portions of the image,
and reports that this also occurred before the change and on the Grande. This
confirms the perceived speed improvement, but does not establish tear-free output
or a separate colour-validation result. The standard Pro build still uses the
baseline driver.
