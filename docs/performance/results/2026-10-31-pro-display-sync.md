# Color Kit Pro synchronization and hardware revision — 2026-10-31

## Outcome

Complete-frame staging in the current quad-PSRAM makes this scene slower. The
scan-line synchronization experiment could not run: three readback approaches
returned implausible data, so the benchmark skipped that profile rather than
using a false synchronization signal. Tearing has not been fixed by this test.

For the user's priority of maximum smoothness, including an extensive hardware
revision, evaluate **ESP32-P4 + a supported 60 Hz MIPI-DSI panel + PSRAM + a wireless
companion** before committing the next PCB. This is an engineering recommendation,
not a measured performance claim for this app on P4.

## Measurements

Same connected ESP32-S3 revision 0.2 / N16R2, 2 MiB quad-PSRAM, 320 × 480 portrait.
Arduino-ESP32 3.3.12 / ESP-IDF 5.5.5, LovyanGFX 1.2.36, LVGL 9.6.0, debug builds.
Network off. Sliding cards/text, full redraws, 10 warmups + 90 measured frames,
three repeats with profile order rotated. Values below are medians of run means
and run P95s. These are complete synchronous refresh durations, not launcher FPS,
LCD scan rate, or input latency. Clocks are configured values, not scope readings.

| Configured clock | Rendering | Mean refresh | P95 | Transfer/copy time |
|---|---|---:|---:|---:|
| 40 MHz | 64-row internal buffer | 52.679 ms | 61.816 ms | 7.820 ms |
| 40 MHz | Full frame in PSRAM | 84.130 ms | 95.446 ms | 19.159 ms |
| 20 MHz | 64-row internal buffer | 56.327 ms | 65.359 ms | 11.638 ms |
| 20 MHz | Full frame in PSRAM | 87.956 ms | 99.252 ms | 23.001 ms |

The 40 MHz full-frame path is 59.7% slower than strips. Its one LVGL flush copies
307,200 bytes from PSRAM into the same internal DMA staging buffer in 64-row bands.
The strip path has eight flushes, rendering directly into the internal buffer.
Thus this compares two practical rendering/transfer pipelines, not isolated PSRAM
bandwidth. Both rendering into PSRAM and staging costs contribute to the result.

Halving the configured write clock costs only 6.9% in complete strip refresh time.
At 40 MHz the transfer accounts for about 15% of refresh time. Even eliminating
that transfer entirely leaves roughly 45 ms of work, far above the 16.7 ms budget
for 60 newly rendered frames per second. Rendering optimization remains useful,
but a faster LCD clock alone cannot meet that target for this measured scene.

Raw initial comparisons:

- [40 MHz](2026-10-31-pro-sync-40mhz.log)
- [20 MHz](2026-10-31-pro-sync-20mhz.log)

The original baseline-to-DMA improvement remains documented in the
[earlier benchmark](2026-10-31-pro-display.md). Small differences from its 51.715 ms
result reflect the separate benchmark build and scene setup; use within-build
comparisons for the staging result.

## Readback diagnosis

The standard driver returned ID `08080000`, power `00`, MADCTL `00`, and only four
scan values: 0, 8, 16, 24. It never detected a plausible scan rollover in 600 ms.
This is insufficient to identify the controller or establish scan timing.

At 20 MHz, two additional diagnostics used 2 microsecond settling and sampled all
16 physical data inputs, avoiding the driver's byte-packing assumptions:

1. [Slow reads using the driver's bus turnaround](2026-10-31-pro-sync-20mhz-readcheck.log).
2. [Explicit GPIO input mode and disconnection of peripheral outputs during reads](2026-10-31-pro-sync-20mhz-turnaround.log), restoring the output mapping with CS inactive.

Both returned raw ID words `0000,0000,0008,0008` and the same four scan values.
Mean read latency rose from about 13 microseconds to 34 and 82 microseconds,
without valid scan-line data. The final diagnostic repeated the rendering tests:
55.892 ms strips / 87.563 ms full-frame, consistent with the initial 20 MHz result.
No scan-sync timing or visual success is reported. This does not isolate the cause
to hardware: controller identity, module read wiring, and signals at the flex
connector need checking with a logic analyzer or scope before that conclusion.

The [ILI9488 datasheet](https://files.waveshare.com/upload/2/2d/ILI9488_Data_Sheet.pdf),
section 5.2.38 / page 206, documents command 0x45 and dummy/high/low read cycles.
Its DBI timing table, section 17.4.1 / page 329, specifies a minimum 40 ns write
cycle and 15 ns high/low pulses. Consequently, a real 40 MHz WR signal would exceed
this published timing. Measure WR and qualify the actual panel/module with its
supplier; do not adopt a higher clock as the next optimization. The 20 MHz result
shows that a conservative clock has only a modest cost here.

## Hardware direction

### Preferred: P4 with synchronized display scanout

Use a supported 60 Hz MIPI-DSI video-mode panel at the required physical size and
resolution, PSRAM with room for at least two complete framebuffers and assets, and
an ESP32-C6 wireless companion. Espressif's
[P4 Function EV Board](https://docs.espressif.com/projects/esp-dev-kits/en/latest/esp32p4/esp32-p4-function-ev-board/user_guide.html)
provides a reference design with P4, PSRAM, DSI and C6 connectivity. Prototype with
a supported board/panel combination first; the reference board's large panel is
not a recommendation for the final product size.

The intended firmware renders the next complete frame while the display scans
an immutable previous frame, and presents completed buffers at frame boundaries.
Use the driver's buffer-ownership and VSYNC facilities correctly; simply allocating
two buffers is insufficient. The [P4 DSI driver](https://docs.espressif.com/projects/esp-idf/en/stable/esp32p4/api-reference/peripherals/lcd/dsi_lcd.html)
supports multiple framebuffers and distinguishes copying completion from safe
framebuffer reuse and VSYNC. P4's [PPA](https://docs.espressif.com/projects/esp-idf/en/stable/esp32p4/api-reference/peripherals/ppa.html)
can accelerate supported fill, blend, scale and rotation work when integrated;
it does not automatically accelerate every LVGL operation or guarantee 60 FPS.

This requires a board and firmware port: display HAL/BSP, wireless companion,
power rails, DSI routing and connector, and touch integration. Do not automatically
increase resolution: more pixels consume the performance headroom being sought.
Use the vendor reference power/layout design and expose useful debug/test points.
An S3 + RGB panel with VSYNC and octal PSRAM is an intermediate option, but retains
the S3 rendering ceiling and adds continuous scanout/memory traffic. It is not the
first choice when maximum smoothness outweighs redesign effort.

Before freezing a custom PCB, run the actual launcher on the candidate board at
the intended resolution and color format. Target stable 60 Hz presentation,
render/submit work within 16.7 ms including P95, no tearing during full-screen
swipes, and measured touch-to-display latency. Test with Wi-Fi, HA updates, asset
loading and normal services active. Record dropped/late frames and use a camera
or scope for presentation/input latency; CPU timing alone cannot prove smoothness.

### If retaining S3 / ILI9488

- Route the panel's actual TE output to an interrupt-capable GPIO and a test pad;
  obtain the module/flex pinout first. The supplied schematic shows no TE route.
  GPIO44 appears to be a candidate if UART RX is unused, subject to a full pin audit.
- Keep RD and expose RD/WR/CS/data test access to resolve register reads. TE reduces
  polling overhead, but is a timing signal, not a framebuffer or rendering accelerator.
- Consider an octal-PSRAM module for more memory and bandwidth, then benchmark it.
  **N16R8 is not a drop-in change with the current pin assignments:** GPIO35/36/37
  are reserved for octal PSRAM and currently carry WR/RS/CS. Remap those controls
  and configure the correct memory mode. Moving buttons to an I2C expander could
  free pins. Verify USB, strapping and all peripheral assignments. See the
  [S3-WROOM-1/1U datasheet](https://www.espressif.com/sites/default/files/documentation/esp32-s3-wroom-1_wroom-1u_datasheet_en.pdf),
  pin definitions and module variants.
- Keep the 16-bit bus, qualify electrical timing, and measure the revised system.
  These changes offer a less disruptive path, but no evidence here guarantees
  sustained 60 FPS on the S3 or tear-free presentation from TE alone.

## Firmware provenance and restoration

Only the existing app slot at 0x10000 was flashed. The partition table matched;
bootloader, partition table, settings/NVS and filesystem were not written by the
flashing operations. The full app was backed up before testing and its image hash
validated. It was restored afterward; boot verified the 64-row internal DMA buffer and restored Home route.

SHA-256 of flashed benchmark images:

| Run | SHA-256 |
|---|---|
| Initial 40 MHz | `3325db580d73fa3f9aeb9026e8441cae327341feb462a31c76bc40355a74911c` |
| Initial 20 MHz | `38d0351e81057ef0cbf9989e35e8d1eeaad16e771bff6842cdb88c4e6533160d` |
| Slow read diagnostic | `341f92e058574476cabac6966232abf66b6fd97d13eb66114ffe0f35d9b8413a` |
| Explicit turnaround | `1cdc5e9af02d8137f257e0d50bc421e30f589c44160df1c603de04e206972ccf` |

The repository's experiment source includes the final readback fallback at 20 MHz.
The first two images predate those diagnostic additions. Their logs and image
hashes identify the measurements; the ordinary app's default clock and read path
remain unchanged. Restored application SHA-256:
`1ee449c53d0f2942b6fee6133288dcb1aa1734b1d2883b6020036e540b1b26d4`.

Restoration boot verification (30-second serial capture, no panic/assert observed):

```text
[UI][Boot] draw buffer rows=64 bytes=40960 memory=internal-bulk-dma
[UI][Boot] internal RAM free=101632 largest=59380
[UI][Boot] current_route=home
```

The flash tool verified the written image hash. The standalone diagnostic built
successfully and all four hardware runs reached `SYNC_DONE`; each recorded three
repetitions of both runnable profiles and three explicit sync skips.
