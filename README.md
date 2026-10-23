# Gadget OS

[The ThingPulse Color Kit Grande](https://thingpulse.com/lp/color-kit-offer/en/) is a fun hands-on soldering and programming project for building your own ESP32 touchscreen gadget. Gadget OS is optimized for the Color Kit Grande.

Gadget OS is an LVGL-based application shell for ThingPulse Color Kit devices. It provides a touch-oriented home screen, persistent settings, background network services, and a native SDL simulator for developing the UI without flashing hardware.

The project is under active development. The main launcher shows Weather, Planes and Calculator on the first row, with Settings on the second row. Experimental apps remain on the Labs page.

For device setup and everyday operation, see the [User Guide](USER_GUIDE.md).

## Supported targets

| PlatformIO environment | Device | MCU |
| --- | --- | --- |
| `native` | Desktop SDL simulator | Host computer |
| `esp32-color-kit` | ThingPulse Color Kit Grande | ESP32 |
| `esp32-color-kit-pro` | ThingPulse Color Kit Pro | ESP32-S3 |

## Features

- Swipeable app launcher with active and experimental apps
- Weather for multiple saved locations, hourly and daily forecasts, cached results, and pull-to-refresh
- Nearby-aircraft radar, plain-language spotting cards, flight-details dialog, and route enrichment
- Calculator
- Wi-Fi, time zone, display, weather, and application settings
- System information page with live heap, PSRAM, LVGL memory, and uptime values
- User-approved, dual-partition OTA updates with stable and prerelease channels
- Automatic Wi-Fi reconnection and NTP synchronization retries
- Optional LVGL performance monitor and serial performance logging
- Native SDL build for faster UI development

Network data currently comes from services including Open-Meteo, adsb.lol, and adsbdb. These integrations do not currently require users to enter subscription API keys, but they remain subject to the respective providers' availability and usage policies.

## Prerequisites

- [pioarduino](https://github.com/pioarduino/platform-espressif32) Core 6.2.0 or its VS Code extension
- A C/C++ toolchain supported by PlatformIO
- SDL2 for the native simulator

On macOS with Homebrew:

```bash
brew install sdl2
```

On Debian or Ubuntu:

```bash
sudo apt install libsdl2-dev
```

Install the pinned build CLI (the `pio` and `platformio` commands remain the same):

```bash
python3 -m venv .venv-build
source .venv-build/bin/activate
python -m pip install -r requirements-build.txt
```

On Windows, activate with `.venv-build\Scripts\activate` instead.

Activate this environment in each new terminal before using `pio`. If another
installation (for example, Conda) takes precedence on your PATH, invoke the
project's CLI explicitly:

```bash
.venv-build/bin/pio --version # Must report 6.2.0
.venv-build/bin/pio run -e esp32-color-kit
```

The ESP32 targets pin pioarduino **55.03.312-1**, using Arduino-ESP32 **3.3.12**
and ESP-IDF **5.5.5**. Stock PlatformIO Core 6.1.x cannot build this platform.
The platform downloads its toolchains and libraries on the first build and may
also create or update the shared `~/.platformio/penv` tool environment.

## Native simulator

Build only:

```bash
pio run -e native
```

Build and start the simulator:

```bash
pio run -e native -t upload
```

You can also start an already-built executable directly:

```bash
.pio/build/native/program
```

Specifying `-e native` means the default environment selected in the VS Code status bar does not need to be changed.

## Color Kit Grande

Connect the device by USB and build and upload with:

```bash
pio run -e esp32-color-kit -t upload
```

Open the serial monitor at 115200 baud:

```bash
pio device monitor -e esp32-color-kit
```

To build without uploading:

```bash
pio run -e esp32-color-kit
```

## Color Kit Pro

Build and upload:

```bash
pio run -e esp32-color-kit-pro -t upload
```

Open the serial monitor:

```bash
pio device monitor -e esp32-color-kit-pro
```

## First-time setup

Configuration is performed on the device under **Settings**:

1. Open **Wi-Fi**, enter the network credentials, and save them.
2. Open the time settings and select the time zone and clock format.
3. Open **Weather** to search for and save one or more locations.
4. Use **Display** to enable the LVGL performance monitor when diagnosing rendering performance.

**Settings → Weather → Weather layout** selects **Detailed** (with hourly forecasts) or **Classic** (the default, a clock-first dashboard with current conditions, four-day forecast, sun times, and a textured moon phase). The choice is saved automatically and applies when opening Weather. Existing explicit choices are preserved after updates; installations without a saved choice use Classic. On first opening Classic, a one-time dialog explains where to find Detailed; dismissing it is remembered. Both layouts support the location carousel. In Classic, tap the daily forecast row (marked **Hourly ›**) to open the upcoming hourly forecast in a scrollable overlay, then tap **Close** to return. The overlay uses the existing hourly data for the selected location, with local times and the configured temperature units. Weather clocks and dates use the selected location’s UTC offset from the forecast response, while retaining the device’s 12/24-hour preference. This offset is cached per location and refreshed with weather data (including seasonal offset changes); offline clocks retain the last known offset. Until the offset is available, the clock shows a placeholder. The device-wide time zone is unchanged. With synchronized time, the moon uses the selected location and current UTC time for its phase and apparent tilt, updating once per minute. The softly shaded icon uses an approximate astronomical model (not a telescope ephemeris); its reference texture does not model lunar libration. Before time synchronization, it falls back to the cached date-based phase without a local tilt. Lunar calculations are adapted from [SunCalc](https://github.com/mourner/suncalc/tree/v1.9.0), under its [BSD license](docs/licenses/SunCalc.txt).

The active weather location is shown on the home screen. Locations can be switched from within the Weather app. Cached weather remains visible when a refresh fails.

Wi-Fi reconnects indefinitely using a bounded backoff. A failed initial NTP synchronization also retries periodically and is attempted immediately after Wi-Fi reconnects.

## Project structure

```text
.app-market/     ESP App Market release configuration
.github/         GitHub Actions workflows
assets/          Source assets
data/            Runtime filesystem data
include/         Public headers
scripts/         Release tooling
src/apps/        LVGL applications
src/services/    Persistent, network, and system services
src/ui/          Shared navigation, status, and icon components
platformio.ini   Build environments and board configuration
```

User-guide screenshots can be regenerated from the native app with:

```bash
./scripts/update-user-guide-screenshots.sh
```

The script uses sanitized fixture data and captures the application framebuffer directly. See [Updating this guide's screenshots](USER_GUIDE.md#updating-this-guides-screenshots) for details.

Generate the real native instructional walkthrough with FFmpeg installed:

```bash
./scripts/update-user-guide-walkthrough.sh
```

This records the live LVGL UI according to
`documents/user-guide/tutorial.json` and writes
`documents/user-guide/video/gadget-os-walkthrough.mp4`. The separate
`update-user-guide-video.sh` command continues to generate the older
screenshot-based `gadget-os-tour.mp4` storyboard. See
[Updating the instructional movie](USER_GUIDE.md#updating-the-instructional-movie)
for the recording and verification details.

Applications register themselves with `APP_REGISTER...` macros, while process-lifetime services use `SERVICE_REGISTER`. UI objects must only be manipulated from the LVGL thread; network work is performed asynchronously and publishes its results back to the UI.

## Creating a GitHub release

The repository creates releases automatically when a semantic-version tag beginning with `v` is pushed.

First commit and push the intended release state:

```bash
git add .
git commit -m "Prepare Gadget OS release"
git push
```

Create and push an annotated tag:

```bash
git tag -a v0.1.4 -m "Gadget OS v0.1.4"
git push origin v0.1.4
```

The **Build and release firmware for ESP App Market** workflow then:

1. Builds the Color Kit Grande and Color Kit Pro firmware.
2. Prepares and validates the ESP App Market manifests.
3. Creates a GitHub release.
4. Uploads the icon, manifests, bootloaders, partition tables, OTA initialization data, and firmware images.

Progress and errors are visible in the repository's **Actions** tab.

Use a suffix for a prerelease:

```bash
git tag -a v0.1.0-beta.1 -m "Gadget OS v0.1.0 beta 1"
git push origin v0.1.0-beta.1
```

Tags containing `-` are marked as prereleases. The workflow's manual **Run workflow** action builds and validates artifacts but intentionally does not publish a release.

If a tag-triggered release fails and no release was published, fix and push the underlying commit, then recreate the tag:

```bash
git tag -d v0.1.0
git push origin :refs/tags/v0.1.0
git tag -a v0.1.0 -m "Gadget OS v0.1.0"
git push origin v0.1.0
```

## ESP App Market flash layout

The release manifests use the flash layout generated by PlatformIO:

| Image | Color Kit Grande | Color Kit Pro |
| --- | ---: | ---: |
| Bootloader | `0x1000` | `0x0` |
| Partition table | `0x8000` | `0x8000` |
| OTA initialization data | `0xe000` | `0xe000` |
| Application firmware | `0x10000` | `0x10000` |

The target-specific release filenames and source paths are defined in [`.app-market/config.json`](.app-market/config.json).

## Troubleshooting

### PlatformIO builds the wrong target

Always provide the environment explicitly, for example:

```bash
pio run -e native
pio run -e esp32-color-kit -t upload
```

### Device is not detected

List available serial ports:

```bash
pio device list
```

Then provide the port explicitly if necessary:

```bash
pio run -e esp32-color-kit -t upload --upload-port /dev/your-device
```

### Network data does not appear

Check the serial monitor for Wi-Fi, DNS, HTTP, TLS, and memory errors. The ESP32 has limited internal RAM, and TLS connections require a sufficiently large contiguous internal allocation even when PSRAM is available.

### Native window does not open

Confirm SDL2 is installed and that PlatformIO links against the correct SDL2 installation. The native executable is located at `.pio/build/native/program` after a successful build.

### Nearby aircraft

Planes opens on a north-up radar centred on the location configured in **Settings → Planes**, with a session-only 25 km radius. Choose 5, 10, 25 or 50 km from the range control (shown in nautical miles with imperial units). Nearby mode filters ground traffic before retaining the nearest 20 aircraft. Tap a radar aircraft or use the footer arrows to select it; the selection stays on that aircraft across refreshes until it leaves the results. The app records the last 20 distinct observed positions for each of up to 20 tracked aircraft, using fixed-size circular buffers (about 3.4 KiB total). Only the selected aircraft’s fading trail is drawn; switching aircraft reveals its existing history. Changing the radius rescales and clips the existing trail. Departed aircraft histories are reclaimed as slots are needed, and changing the configured location clears all histories. History lives only in memory for the current app session. The highlighted card gives a compass direction, distance, and movement relative to your configured location; tap it for full details. Altitude is above sea level, not height above the observer. Missing headings are drawn as dots and do not produce movement claims.

The radar is the only main view. The footer’s Previous/Next arrows cycle through every nearby aircraft (with a selection index), including overlapping radar markers. Available origin/destination airport codes appear on the selected card; missing or partial routes are omitted. Tap the card for an organized flight-details dialog: airline and aircraft name, prominent airport codes with airport names capped at two lines (ellipsis for longer names), a grid of altitude/speed/distance/direction, and secondary registration/ICAO/squawk details. Long content scrolls while **Close** stays visible. Route information is fetched by callsign from ADSBdb on subsequent refreshes (normally every 15 seconds), with up to three uncached lookups per cycle. The selected aircraft gets first priority; selections changed during an active request take priority on the next refresh. Successful routes are cached for six hours, misses for 30 minutes. Routes are not available for every flight. Back returns home. Session range/filter changes never overwrite settings. The footer shows data age or an update error, and empty results offer a larger radius. The radar needs no map tiles, compass sensor, or extra network calls beyond the aircraft feed. The configured location is labelled “You”; it does not track device movement.
