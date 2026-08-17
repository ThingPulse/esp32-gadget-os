#!/usr/bin/env bash
set -euo pipefail

SHOW_CAPTIONS=1
OUTPUT_OVERRIDE=""

usage() {
  cat <<'EOF'
Usage: scripts/update-blog-feature-video.sh [options]

Options:
  --no-captions, --no-text  Hide the explanatory caption overlays
  --with-captions           Show caption overlays (default)
  --output PATH             Write the finished video to PATH
  -h, --help                Show this help
EOF
}

while (( $# )); do
  case "$1" in
    --no-captions|--no-text) SHOW_CAPTIONS=0; shift ;;
    --with-captions) SHOW_CAPTIONS=1; shift ;;
    --output)
      [[ $# -ge 2 ]] || { echo "--output requires a path" >&2; exit 2; }
      OUTPUT_OVERRIDE="$2"; shift 2 ;;
    -h|--help) usage; exit 0 ;;
    *) echo "Unknown option: $1" >&2; usage >&2; exit 2 ;;
  esac
done

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
SOURCE_FIXTURE="$PROJECT_DIR/documents/user-guide-fixture"
BLOG_DIR="$PROJECT_DIR/documents/blog"
BUILD_DIR="$BLOG_DIR/video/.build"
RUNTIME_FIXTURE="$BUILD_DIR/fixture"
VARIANT_SUFFIX=""
if (( ! SHOW_CAPTIONS )); then VARIANT_SUFFIX="-no-text"; fi
PORTRAIT_VIDEO="$BUILD_DIR/gadget-os-feature-tour${VARIANT_SUFFIX}-native.mp4"
OUTPUT="${OUTPUT_OVERRIDE:-$BLOG_DIR/video/gadget-os-feature-tour${VARIANT_SUFFIX}.mp4}"
PROGRAM="$PROJECT_DIR/.pio/build/native/program"
TUTORIAL_SOURCE="$BLOG_DIR/gadget-os-feature-tour.json"
TUTORIAL="$TUTORIAL_SOURCE"
CONTACT_SHEET="$BUILD_DIR/contact-sheet${VARIANT_SUFFIX}.png"
FFMPEG="${FFMPEG:-/opt/homebrew/bin/ffmpeg}"
FFPROBE="${FFPROBE:-$(dirname "$FFMPEG")/ffprobe}"

command -v pio >/dev/null || { echo "PlatformIO (pio) is required" >&2; exit 1; }
[[ -x "$FFMPEG" ]] || { echo "ffmpeg is required (set FFMPEG if it is elsewhere)" >&2; exit 1; }
[[ -x "$FFPROBE" ]] || { echo "ffprobe is required (set FFPROBE if it is elsewhere)" >&2; exit 1; }

mkdir -p "$BUILD_DIR"
mkdir -p "$(dirname "$OUTPUT")"
rm -rf "$RUNTIME_FIXTURE"
cp -R "$SOURCE_FIXTURE" "$RUNTIME_FIXTURE"

# Keep all action durations unchanged in the caption-free variant so both
# renders have identical cuts and gestures.
if (( ! SHOW_CAPTIONS )); then
  TUTORIAL="$BUILD_DIR/gadget-os-feature-tour-no-text.json"
  node -e '
    const fs = require("fs");
    const source = JSON.parse(fs.readFileSync(process.argv[1], "utf8"));
    for (const action of source.actions) {
      if (action.type === "caption") action.text = "";
    }
    fs.writeFileSync(process.argv[2], JSON.stringify(source, null, 2) + "\n");
  ' "$TUTORIAL_SOURCE" "$TUTORIAL"
fi

PATH="$(dirname "$FFMPEG"):$PATH" pio run --project-dir "$PROJECT_DIR" -e native
DURATION_MS="$(node -e 'const d=require(process.argv[1]); console.log(d.actions.reduce((n,a)=>n+(a.duration_ms||700),0)+3500)' "$TUTORIAL")"

(
  cd "$PROJECT_DIR"
  ASAN_OPTIONS=detect_leaks=0 \
  GADGET_OS_DATA_DIR="$RUNTIME_FIXTURE" \
  GADGET_OS_TUTORIAL_PATH="$TUTORIAL" \
  GADGET_OS_VIDEO_PATH="$PORTRAIT_VIDEO" \
  GADGET_OS_VIDEO_DURATION_MS="$DURATION_MS" \
  "$PROGRAM"
)

# Put the native portrait recording into a blog-friendly 16:9 canvas. The
# foreground remains pixel-sharp while a darkened, blurred copy fills the sides.
"$FFMPEG" -hide_banner -loglevel error -y -i "$PORTRAIT_VIDEO" -filter_complex \
  "[0:v]scale=1280:720:force_original_aspect_ratio=increase,crop=1280:720,boxblur=28:14,eq=brightness=-0.30:saturation=0.75[bg];[0:v]scale=420:630:flags=neighbor[fg];[bg][fg]overlay=(W-w)/2:(H-h)/2" \
  -r 30 -c:v libx264 -crf 18 -preset medium -pix_fmt yuv420p -movflags +faststart \
  "$OUTPUT"

"$FFMPEG" -hide_banner -loglevel error -y -i "$OUTPUT" -vf \
  "select='eq(n,180)+eq(n,600)+eq(n,1020)+eq(n,1440)+eq(n,1860)+eq(n,2280)',scale=480:270,tile=3x2" \
  -frames:v 1 "$CONTACT_SHEET"

"$FFPROBE" -v error -select_streams v:0 \
  -show_entries stream=codec_name,width,height,r_frame_rate,pix_fmt:format=duration \
  -of default=noprint_wrappers=1 "$OUTPUT"
echo "Blog feature tour: $OUTPUT"
echo "Captions: $([[ $SHOW_CAPTIONS -eq 1 ]] && echo shown || echo hidden)"
echo "Representative frames: $CONTACT_SHEET"
