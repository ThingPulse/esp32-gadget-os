#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
FIXTURE_DIR="$PROJECT_DIR/documents/user-guide-fixture"
OUTPUT_DIR="$PROJECT_DIR/documents/user-guide/screenshots"
PROGRAM="$PROJECT_DIR/.pio/build/native/program"
LANGUAGE_CODE="${1:-en}"
case "$LANGUAGE_CODE" in
  en) ;;
  de) OUTPUT_DIR="$OUTPUT_DIR/de" ;;
  *) echo "Usage: $0 [en|de]" >&2; exit 2 ;;
esac

# Work on a copy so captures cannot change the checked-in demo settings.
WORK_DIR="$(mktemp -d "${TMPDIR:-/tmp}/gadget-screenshots.XXXXXX")"
trap 'rm -rf "$WORK_DIR"' EXIT
cp -R "$FIXTURE_DIR/." "$WORK_DIR/"
if [[ "${GADGET_OS_SKIP_BUILD:-0}" != 1 ]]; then
  pio run --project-dir "$PROJECT_DIR" -e native
fi
python3 - "$WORK_DIR" <<'PYFIXTURE'
import json
import sys
from datetime import datetime, timedelta, timezone
from pathlib import Path
root = Path(sys.argv[1])
weather_path = root / 'config/weather.json'
weather = json.loads(weather_path.read_text())
weather['classic_intro_seen'] = True
weather_path.write_text(json.dumps(weather))
(root / 'tutorial.json').write_text(json.dumps({'actions': []}))
(root / 'config/transit.json').write_text(json.dumps({
    'provider': 'transitous',
    'favourites': json.dumps([{'id': 'demo-zurich', 'name': 'Zurich HB', 'area': 'CH'}])
}))
now = datetime.now(timezone.utc)
def iso(minutes):
    return (now + timedelta(minutes=minutes)).strftime('%Y-%m-%dT%H:%M:%SZ')
departures = []
for line, destination, minutes, platform, delay, live, color in [
    ('S8', 'Winterthur', 3, '41', 0, True, '168CF0'),
    ('IR13', 'St. Gallen', 8, '7', 2, True, 'DB4545'),
    ('S24', 'Zug', 14, '4', 0, False, '8056DB'),
]:
    departures.append({'displayName': line, 'headsign': destination,
        'tripId': 'demo-trip', 'realTime': live, 'routeColor': color,
        'place': {'departure': iso(minutes), 'scheduledDeparture': iso(minutes-delay), 'track': platform}})
(root / 'board.json').write_text(json.dumps({'stopTimes': departures}))
(root / 'search.json').write_text('[]')
(root / 'trip.json').write_text('{"legs": []}')
PYFIXTURE

mkdir -p "$OUTPUT_DIR"

capture() {
  local route="$1"
  local name="$2"
  local ppm="$OUTPUT_DIR/$name.ppm"
  echo "Capturing $name ($route)"
  if [[ "$route" == transit ]]; then
    python3 - "$WORK_DIR/board.json" <<'PYTIMES'
import json
import sys
from datetime import datetime, timedelta, timezone
from pathlib import Path
path = Path(sys.argv[1])
data = json.loads(path.read_text())
first = datetime.fromisoformat(data['stopTimes'][0]['place']['departure'].replace('Z', '+00:00'))
shift = datetime.now(timezone.utc) + timedelta(minutes=3) - first
for departure in data['stopTimes']:
    for key in ('departure', 'scheduledDeparture'):
        old = datetime.fromisoformat(departure['place'][key].replace('Z', '+00:00'))
        departure['place'][key] = (old + shift).strftime('%Y-%m-%dT%H:%M:%SZ')
path.write_text(json.dumps(data))
PYTIMES
  fi
  (
    cd "$PROJECT_DIR"
    ASAN_OPTIONS=detect_leaks=0 \
      GADGET_OS_LANGUAGE="$LANGUAGE_CODE" \
      GADGET_OS_DATA_DIR="$WORK_DIR" \
      GADGET_OS_TUTORIAL_PATH="$WORK_DIR/tutorial.json" \
      GADGET_OS_STOCK_FIXTURE=1 \
      GADGET_OS_TRANSIT_FIXTURE_DIR="$WORK_DIR" \
      GADGET_OS_DOC_ROUTE="$route" \
      GADGET_OS_SCREENSHOT_PATH="$ppm" \
      GADGET_OS_SCREENSHOT_DELAY_MS=3000 \
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
capture calculator calculator
capture planes planes
capture stocks stocks
capture transit transit
capture dice_game dice-score
capture weather_icon_test weather-icons

echo "Updated screenshots in $OUTPUT_DIR"
