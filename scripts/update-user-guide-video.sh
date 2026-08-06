#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
SCREENSHOTS="$PROJECT_DIR/documents/user-guide/screenshots"
VIDEO_DIR="$PROJECT_DIR/documents/user-guide/video"
WORK_DIR="$VIDEO_DIR/.build"
OUTPUT="$VIDEO_DIR/gadget-os-tour.mp4"

command -v ffmpeg >/dev/null || { echo "ffmpeg is required" >&2; exit 1; }
mkdir -p "$WORK_DIR"
"$SCRIPT_DIR/update-user-guide-screenshots.sh"

render_scene() {
  local number="$1" image="$2" duration="$3" caption="$4" touch_x="$5" touch_y="$6"
  local output="$WORK_DIR/scene-$number.mp4"
  local filter="scale=640:960:flags=neighbor,drawbox=x=0:y=830:w=640:h=130:color=black@0.72:t=fill,drawtext=fontfile=/System/Library/Fonts/Supplemental/Arial.ttf:text='$caption':fontcolor=white:fontsize=30:x=(w-text_w)/2:y=865"
  if [[ "$touch_x" != "-" ]]; then
    filter="$filter,drawbox=x=$((touch_x * 2 - 18)):y=$((touch_y * 2 - 18)):w=36:h=36:color=white@0.75:t=4"
  fi
  ffmpeg -hide_banner -loglevel error -y -loop 1 -framerate 30 -i "$SCREENSHOTS/$image.png" \
    -t "$duration" -vf "$filter" -r 30 -c:v libx264 -pix_fmt yuv420p "$output"
}

render_scene 01 wifi 3.0 "Open Wi-Fi settings and scan for networks" 160 162
render_scene 02 wifi 3.0 "Select the network, then enter its password" 160 108
render_scene 03 time 4.0 "Choose the region, time zone and clock format" 160 330
render_scene 04 weather-locations 3.5 "Search for Zurich and select the result" 160 270
render_scene 05 weather-locations 3.5 "Add New York as a second saved location" 120 190
render_scene 06 weather 3.5 "Open Weather and pull down to refresh" 160 130
render_scene 07 weather 3.5 "Use the arrows to switch to the second location" 274 80
render_scene 08 weather 3.5 "Swipe the hourly forecast horizontally" 210 305
render_scene 09 weather 3.5 "Scroll down to see wind information" 285 410
render_scene 10 home 2.0 "Return to the home screen" 45 45
render_scene 11 calculator 4.0 "Open Calculator and enter 12 × 7 = 84" 160 360
render_scene 12 home 3.0 "Swipe left to reveal the Lab apps" 245 260
render_scene 13 planes 5.0 "Open Planes to explore nearby aircraft" 160 200

CONCAT_FILE="$WORK_DIR/scenes.txt"
: > "$CONCAT_FILE"
for scene in "$WORK_DIR"/scene-*.mp4; do
  printf "file '%s'\n" "$scene" >> "$CONCAT_FILE"
done
ffmpeg -hide_banner -loglevel error -y -f concat -safe 0 -i "$CONCAT_FILE" -c copy -movflags +faststart "$OUTPUT"
echo "Created $OUTPUT"
