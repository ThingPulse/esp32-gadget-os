#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
font=.pio/libdeps/native/lvgl/scripts/built_in_font/Montserrat-Medium.ttf
# Small fallback fonts preserve the built-in Latin and icon fonts.
for size in 14 24; do
  npx --yes --package=lv_font_conv@1.5.3 lv_font_conv --size "$size" --bpp 4 --format lvgl --lv-include lvgl.h \
    --font "$font" --symbols 'ÄÖÜäöüß–' --no-compress --no-prefilter \
    --lv-font-name "german_glyphs_$size" -o "src/assets/GermanGlyphs$size.c"
done
