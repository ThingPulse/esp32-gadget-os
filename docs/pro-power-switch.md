# Color Kit Pro power switch

In normal Pro firmware, pull the switch down and release it to turn the device
off (ESP32 deep sleep, LCD asleep, backlight off). Pull down again to wake it.
Wake performs a normal boot into the home screen, preserving saved settings;
unsaved edits and in-memory app state do not survive. The wake gesture must be
released before another pull can switch the device off.

This uses the confirmed active-high GPIO1 contact with the board's external
10 kΩ pulldown. Internal pulls are disabled. Press-in and push-up are unchanged.
The switch is debounced for 40 ms on each edge; sleep begins after release so
the same pull cannot immediately wake the device. Firmware installation blocks
sleep; pull again after the update completes. Standalone diagnostic and display
benchmark builds do not include this behavior.

Deep sleep is not battery disconnection: the regulator, touch controller, gauge
and other board circuitry remain powered. Measure actual battery current on the
prototype; no battery-life claim is implied. USB serial disconnects during sleep.

## Verification

Host regression test (bounce, held wake gesture, repeated pulls, timer wrap):

```sh
c++ -std=c++17 -Iinclude test/power/switch_regression.cpp -o /tmp/pro-power-test
/tmp/pro-power-test
```

Build the existing faster-display firmware:

```sh
.venv-build/bin/pio run -e esp32-color-kit-pro-display-test
```

Hardware checks still required:

1. Pull/release while awake: display and backlight turn off and stay off.
2. Pull while asleep: normal boot; holding the switch must not shut it down.
3. Release, then pull/release again: sleeps; repeat several times.
4. Check saved settings after waking and measure idle battery current in sleep.
5. Verify no backlight glow or buzzer activity during sleep.

Implementation uses Espressif's EXT1 active-high wake source:
https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/api-reference/system/sleep_modes.html
