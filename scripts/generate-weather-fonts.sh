#!/usr/bin/env bash
# Run after installing the project's native dependencies with pio run -e native.
set -euo pipefail
cd "$(dirname "$0")/.."
font=.pio/libdeps/native/lvgl/scripts/built_in_font/Montserrat-Medium.ttf
npx --yes --package=lv_font_conv@1.5.3 lv_font_conv --size 48 --bpp 4 --format lvgl --lv-include lvgl.h \
  --font "$font" --symbols '0123456789:-' --no-compress --no-prefilter \
  --lv-font-name weather_clock_48 -o src/assets/WeatherClock48.c
npx --yes --package=lv_font_conv@1.5.3 lv_font_conv --size 36 --bpp 4 --format lvgl --lv-include lvgl.h \
  --font "$font" --symbols '0123456789.-°' --no-compress --no-prefilter \
  --lv-font-name weather_temperature_36 -o src/assets/WeatherTemperature36.c
