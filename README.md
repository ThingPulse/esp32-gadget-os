# Gadget OS

[The ThingPulse Color Kit Grande](https://thingpulse.com/lp/color-kit-offer/en/) is a fun hands-on soldering and programming project for building your own ESP32 touchscreen gadget. Gadget OS is optimized for the Color Kit Grande.

Gadget OS is an LVGL-based application shell for ThingPulse Color Kit devices. It provides a touch-oriented home screen, persistent settings, background network services, and a native SDL simulator for developing the UI without flashing hardware.

The project is under active development. The Weather app is the most complete network application; the Planes app should currently be considered experimental.

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
- Experimental nearby-aircraft list, details, route enrichment, airline branding, and map
- Calculator
- Wi-Fi, time zone, display, weather, and application settings
- System information page with live heap, PSRAM, LVGL memory, and uptime values
- User-approved, dual-partition OTA updates with stable and prerelease channels
- Automatic Wi-Fi reconnection and NTP synchronization retries
- Optional LVGL performance monitor and serial performance logging
- Native SDL build for faster UI development

Network data currently comes from services including Open-Meteo, adsb.lol, adsbdb, CARTO, and OpenStreetMap contributors. These integrations do not currently require users to enter subscription API keys, but they remain subject to the respective providers' availability and usage policies.

## Prerequisites

- [PlatformIO](https://platformio.org/) Core or the PlatformIO IDE extension
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

PlatformIO downloads the embedded toolchains and project libraries during the first build.

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
git tag -a v0.1.0 -m "Gadget OS v0.1.0"
git push origin v0.1.0
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
