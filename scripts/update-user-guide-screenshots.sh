#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
FIXTURE_DIR="$PROJECT_DIR/documents/user-guide-fixture"
OUTPUT_DIR="$PROJECT_DIR/documents/user-guide/screenshots"
PROGRAM="$PROJECT_DIR/.pio/build/native/program"

mkdir -p "$OUTPUT_DIR"
pio run --project-dir "$PROJECT_DIR" -e native

capture() {
  local route="$1"
  local name="$2"
  local ppm="$OUTPUT_DIR/$name.ppm"
  echo "Capturing $name ($route)"
  (
    cd "$PROJECT_DIR"
    ASAN_OPTIONS=detect_leaks=0 \
      GADGET_OS_DATA_DIR="$FIXTURE_DIR" \
      GADGET_OS_DOC_ROUTE="$route" \
      GADGET_OS_SCREENSHOT_PATH="$ppm" \
      GADGET_OS_SCREENSHOT_DELAY_MS=400 \
      "$PROGRAM"
  )
  node "$SCRIPT_DIR/ppm-to-png.mjs" "$ppm" "$OUTPUT_DIR/$name.png"
  rm -f "$ppm"
}

capture home home
capture settings settings
capture settings_wifi wifi
capture settings_time time
capture settings_weather weather-locations
capture settings_display display
capture settings_system system
capture weather weather

echo "Updated screenshots in $OUTPUT_DIR"
