#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
SOURCE_FIXTURE="$PROJECT_DIR/documents/user-guide-fixture"
BUILD_DIR="$PROJECT_DIR/documents/user-guide/video/.build"
RUNTIME_FIXTURE="$BUILD_DIR/fixture"
OUTPUT="$PROJECT_DIR/documents/user-guide/video/gadget-os-walkthrough.mp4"
PROGRAM="$PROJECT_DIR/.pio/build/native/program"
TUTORIAL="$PROJECT_DIR/documents/user-guide/tutorial.json"
FFMPEG="${FFMPEG:-/opt/homebrew/bin/ffmpeg}"

mkdir -p "$BUILD_DIR"
rm -rf "$RUNTIME_FIXTURE"
cp -R "$SOURCE_FIXTURE" "$RUNTIME_FIXTURE"
# Begin from an unconfigured device while retaining sanitized forecast/aircraft fixtures.
printf '%s\n' '{"ssid":"","password":""}' > "$RUNTIME_FIXTURE/config/wifi.json"
printf '%s\n' '{"timezone_index":0,"use_24h":false}' > "$RUNTIME_FIXTURE/config/time.json"
printf '%s\n' '{"metric":true,"location_count":0,"active_location":0}' > "$RUNTIME_FIXTURE/config/weather.json"

PATH="$(dirname "$FFMPEG"):$PATH" pio run --project-dir "$PROJECT_DIR" -e native
DURATION_MS="$(node -e 'const d=require(process.argv[1]); console.log(d.actions.reduce((n,a)=>n+(a.duration_ms||700),0)+3500)' "$TUTORIAL")"
(
  cd "$PROJECT_DIR"
  ASAN_OPTIONS=detect_leaks=0 \
  GADGET_OS_DATA_DIR="$RUNTIME_FIXTURE" \
  GADGET_OS_TUTORIAL_PATH="$TUTORIAL" \
  GADGET_OS_VIDEO_PATH="$OUTPUT" \
  GADGET_OS_VIDEO_DURATION_MS="$DURATION_MS" \
  "$PROGRAM"
)

"$FFMPEG" -hide_banner -loglevel error -i "$OUTPUT" -vf \
  "select='eq(n,300)+eq(n,900)+eq(n,1500)+eq(n,2100)',scale=320:480,tile=2x2" \
  -frames:v 1 -y "$BUILD_DIR/contact-sheet.png"
ffprobe -v error -select_streams v:0 \
  -show_entries stream=codec_name,width,height,r_frame_rate,pix_fmt \
  -of default=noprint_wrappers=1 "$OUTPUT"
echo "Walkthrough: $OUTPUT"
echo "Representative frames: $BUILD_DIR/contact-sheet.png"
