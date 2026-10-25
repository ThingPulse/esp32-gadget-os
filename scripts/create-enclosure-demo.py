#!/usr/bin/env python3
"""Record a native tutorial and composite the supplied enclosure and RGBA hand footage.
Requires a native build, ffmpeg on PATH, and Pillow. Inputs remain external.
"""
import argparse
import csv
from datetime import datetime, timedelta, timezone
from functools import lru_cache
import json
import math
import os
from pathlib import Path
import shutil
import subprocess

from PIL import Image, ImageChops, ImageFilter

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--background', type=Path, required=True)
    parser.add_argument('--hand', type=Path, required=True)
    parser.add_argument('--output', type=Path, help='Output MP4; German defaults to video/de/ to preserve English videos')
    parser.add_argument('--language', choices=['en', 'de'], default='en',
                        help='UI language for recording and button taps (default: en)')
    parser.add_argument('--sequence', choices=['swipe', 'weather', 'planes', 'stocks', 'transit', 'calculator', 'ota'], default='swipe')
    parser.add_argument('--hand-scale', type=float, default=1.5)
    parser.add_argument('--hand-treatment', choices=['natural', 'original'], default='natural',
                        help='Natural adds inward edge feathering and a soft shadow')
    parser.add_argument('--reuse-recording', type=Path,
                        help='Reuse native.mp4, touch.csv and tutorial.json from a scene build directory')
    parser.add_argument('--preview-seconds', type=float,
                        help='Render only the beginning of the recording for visual comparisons')
    args = parser.parse_args()
    if args.preview_seconds is not None and args.preview_seconds <= 0:
        parser.error('--preview-seconds must be positive')
    if not 0.1 <= args.hand_scale <= 3:
        parser.error('--hand-scale must be between 0.1 and 3')
    default_dir = ROOT / 'documents/user-guide/video'
    if args.language != 'en':
        default_dir /= args.language
    output = (args.output or default_dir / 'enclosure-swipe-demo.mp4').resolve()
    translations = json.loads((ROOT / 'translations/de.json').read_text()) if args.language == 'de' else {}
    def translated(text):
        return translations.get(text, text)
    work = output.parent / '.build' / output.stem
    work.mkdir(parents=True, exist_ok=True)
    if args.reuse_recording:
        native = args.reuse_recording / 'native.mp4'
        trace = args.reuse_recording / 'touch.csv'
        recording = json.loads((args.reuse_recording / 'tutorial.json').read_text())
        recorded_language = recording.get('language', 'en')
        if recorded_language != args.language:
            parser.error(f'Reused recording is {recorded_language}, not {args.language}; record it again with --language {args.language}')
        actions = recording['actions']
    else:
        fixture = work / 'fixture'
        if fixture.exists():
            shutil.rmtree(fixture)
        shutil.copytree(ROOT / 'documents/user-guide-fixture', fixture)
        timeline = work / 'tutorial.json'
        actions = [
            {'type': 'route', 'route': 'home', 'duration_ms': 1000},
            {'type': 'swipe', 'x': 270, 'y': 240, 'x2': 55, 'y2': 240, 'duration_ms': 900},
            {'type': 'wait', 'duration_ms': 2400},
        ]
        if args.sequence == 'weather':
            weather_path = fixture / 'config/weather.json'
            weather = json.loads(weather_path.read_text())
            weather.update(layout='classic', classic_intro_seen=True, active_location=0)
            weather_path.write_text(json.dumps(weather))
            actions = [
                {'type': 'route', 'route': 'home', 'duration_ms': 1000},
                {'type': 'tap_exact', 'text': 'Weather', 'duration_ms': 160},
                {'type': 'wait', 'duration_ms': 2300},
                {'type': 'swipe', 'x': 270, 'y': 145, 'x2': 55, 'y2': 145, 'duration_ms': 900},
                {'type': 'wait', 'duration_ms': 2500},
                {'type': 'tap', 'x': 268, 'y': 245, 'duration_ms': 160},
                {'type': 'wait', 'duration_ms': 3000},
                {'type': 'tap_exact', 'text': 'Close', 'duration_ms': 160},
                {'type': 'wait', 'duration_ms': 1800},
                {'type': 'tap', 'x': 30, 'y': 22, 'duration_ms': 160},
                {'type': 'wait', 'duration_ms': 1800},
            ]
        if args.sequence == 'planes':
            actions = [
                {'type': 'route', 'route': 'home', 'duration_ms': 1000},
                {'type': 'tap_exact', 'text': 'Planes', 'duration_ms': 160},
                {'type': 'wait', 'duration_ms': 1000},
                {'type': 'planes_warmup', 'duration_ms': 30000},
                {'type': 'wait', 'duration_ms': 2300},
                {'type': 'tap', 'x': 288, 'y': 464, 'duration_ms': 160},
                {'type': 'wait', 'duration_ms': 2300},
                {'type': 'tap', 'x': 160, 'y': 385, 'duration_ms': 160},
                {'type': 'wait', 'duration_ms': 3200},
                {'type': 'tap_exact', 'text': 'Close', 'duration_ms': 160},
                {'type': 'wait', 'duration_ms': 1700},
                {'type': 'tap', 'x': 24, 'y': 22, 'duration_ms': 160},
                {'type': 'wait', 'duration_ms': 1800},
            ]
        if args.sequence == 'stocks':
            (fixture / 'config/stocks.json').write_text(json.dumps({
                'symbols': 'AAPL\nNESN.SW\n7203.T\nSAP.DE\nVOD.L', 'provider': 'yahoo'}))
            actions = [{'type': 'route', 'route': 'home', 'duration_ms': 1000}]
            def tap(x, y, wait=1800):
                actions.extend([{'type': 'tap', 'x': x, 'y': y, 'duration_ms': 160},
                                {'type': 'wait', 'duration_ms': wait}])
            actions.extend([{'type': 'tap_exact', 'text': 'Stocks', 'duration_ms': 160},
                            {'type': 'wait', 'duration_ms': 2700}])
            tap(80, 115, 2200)  # AAPL
            tap(285, 74, 2200)  # Next stock: NESN.SW, initially 5D
            # Semantic button taps survive quote-driven UI rebuilds between press and release.
            for period in ['1M', '1Y']:
                actions.extend([{'type': 'tap_exact', 'text': period, 'duration_ms': 160},
                                {'type': 'wait', 'duration_ms': 2700}])
            tap(28, 22)  # Watchlist
            tap(28, 22)  # Close Stocks
        if args.sequence == 'transit':
            (fixture / 'config/transit.json').write_text(json.dumps({
                'provider': 'transitous',
                'favourites': json.dumps([{'id': 'demo-zurich', 'name': 'Zürich HB', 'area': 'CH'}])}))
            now = datetime.now(timezone.utc)
            def departure_time(minutes):
                return (now + timedelta(minutes=minutes)).strftime('%Y-%m-%dT%H:%M:%SZ')
            departures = []
            for line, destination, minutes, platform, delay, live, color in [
                ('S8', 'Winterthur', 3, '41', 0, True, '168CF0'),
                ('IR13', 'St. Gallen', 8, '7', 2, True, 'DB4545'),
                ('S24', 'Zug', 14, '4', 0, False, '8056DB'),
                ('S16', 'Zürich Flughafen', 18, '43', 0, True, '168CF0'),
            ]:
                departures.append({'displayName': line, 'headsign': destination,
                    'tripId': 'demo-trip', 'realTime': live, 'routeColor': color,
                    'place': {'departure': departure_time(minutes),
                              'scheduledDeparture': departure_time(minutes-delay), 'track': platform}})
            (fixture / 'board.json').write_text(json.dumps({'stopTimes': departures}))
            def stop(identifier, name, minutes, platform):
                return {'stopId': identifier, 'name': name, 'departure': departure_time(minutes), 'track': platform}
            (fixture / 'trip.json').write_text(json.dumps({'legs': [{
                'from': stop('demo-zurich', 'Zürich HB', 3, '41'),
                'intermediateStops': [stop('demo-oerlikon', 'Zürich Oerlikon', 10, '6')],
                'to': stop('demo-winterthur', 'Winterthur', 27, '4'),
            }]}))
            (fixture / 'search.json').write_text('[]')
            actions = [
                {'type': 'route', 'route': 'home', 'duration_ms': 1000},
                {'type': 'tap_exact', 'text': 'Transit', 'duration_ms': 160},
                {'type': 'wait', 'duration_ms': 3200},
                {'type': 'tap_exact', 'text': 'Winterthur', 'duration_ms': 160},
                {'type': 'wait', 'duration_ms': 3600},
                {'type': 'tap', 'x': 24, 'y': 22, 'duration_ms': 160},
                {'type': 'wait', 'duration_ms': 2000},
                {'type': 'tap', 'x': 24, 'y': 22, 'duration_ms': 160},
                {'type': 'wait', 'duration_ms': 1800},
            ]
        if args.sequence == 'calculator':
            actions = [
                {'type': 'route', 'route': 'home', 'duration_ms': 1000},
                {'type': 'tap_exact', 'text': 'Calculator', 'duration_ms': 160},
                {'type': 'wait', 'duration_ms': 1800},
            ]
            for key in ['4', '2', '*', '3', '.', '1', '4', '1', '=']:
                actions.extend([{'type': 'tap_exact', 'text': key, 'duration_ms': 120},
                                {'type': 'wait', 'duration_ms': 3000 if key == '=' else 1500}])
            actions.extend([{'type': 'tap', 'x': 28, 'y': 22, 'duration_ms': 160},
                            {'type': 'wait', 'duration_ms': 1800}])
        if args.sequence == 'ota':
            actions = [
                {'type': 'route', 'route': 'home', 'duration_ms': 1800},
                {'type': 'ota_available', 'duration_ms': 2500},
                {'type': 'tap_exact', 'text': 'Install', 'duration_ms': 160},
                {'type': 'ota_progress', 'duration_ms': 6000},
                {'type': 'wait', 'duration_ms': 1200},
                {'type': 'ota_restart', 'duration_ms': 2000},
                {'type': 'ota_installed', 'duration_ms': 2500},
                {'type': 'tap_exact', 'text': 'Close', 'duration_ms': 160},
                {'type': 'wait', 'duration_ms': 1800},
            ]
        # Translate visible labels only; route IDs, symbols, and calculator keys
        # remain stable. Use the same catalogue as the firmware (e.g. 1Y -> 1J).
        for action in actions:
            if action['type'] in ('tap_text', 'tap_exact'):
                action['text'] = translated(action['text'])
        timeline.write_text(json.dumps({'language': args.language, 'sequence': args.sequence,
                                        'actions': actions}, indent=2, ensure_ascii=False))
        duration = sum(a['duration_ms'] for a in actions if a['type'] != 'planes_warmup') + 1700
        native = work / 'native.mp4'
        trace = work / 'touch.csv'
        env = dict(os.environ, ASAN_OPTIONS='detect_leaks=0', GADGET_OS_LANGUAGE=args.language,
                   GADGET_OS_DATA_DIR=str(fixture), GADGET_OS_TUTORIAL_PATH=str(timeline),
                   GADGET_OS_VIDEO_PATH=str(native), GADGET_OS_VIDEO_DURATION_MS=str(duration),
                   GADGET_OS_VIDEO_TOUCH_PATH=str(trace), GADGET_OS_HIDE_TOUCH_DOT='1')
        if args.sequence == 'transit':
            env['GADGET_OS_TRANSIT_FIXTURE_DIR'] = str(fixture)
        if args.sequence == 'ota':
            env['GADGET_OS_OTA_FIXTURE'] = '1'
        if args.sequence == 'stocks':
            env['GADGET_OS_STOCK_FIXTURE'] = '1'
        if args.sequence == 'planes':
            env['GADGET_OS_PLANES_MOVING_FIXTURE'] = '1'
        with (work / 'native.log').open('w') as log:
            subprocess.run([str(ROOT / '.pio/build/native/program')], cwd=ROOT, env=env,
                           stdout=log, stderr=subprocess.STDOUT, check=True, timeout=duration/1000+75)
    rows = list(csv.DictReader(trace.open()))
    frame_count = min(len(rows), math.ceil(args.preview_seconds*30)) if args.preview_seconds else len(rows)
    active = [int(r['frame']) for r in rows if int(r['pressed'])]
    if not active:
        raise RuntimeError('No touches recorded; see native.log')
    gestures = []
    for index in active:
        if not gestures or index > gestures[-1][1] + 1:
            gestures.append([index, index])
        else:
            gestures[-1][1] = index
    # Keyboard presses form one continuous hand performance. Keep the hand
    # visible between keys, with a small lift and eased movement to the next key.
    touch_actions = [a for a in actions if a['type'] in ('tap', 'tap_text', 'tap_exact', 'swipe', 'key')]
    bridges = []
    if args.sequence == 'stocks':
        if len(touch_actions) != len(gestures):
            raise RuntimeError('Touch trace does not match the tutorial actions')
        for n in range(len(gestures)-1):
            if touch_actions[n]['type'] == touch_actions[n+1]['type'] == 'key':
                bridges.append((gestures[n][1], gestures[n+1][0]))
    # The supplied 1920x1080 photograph has a front-facing 504x750 display.
    background = Image.open(args.background).convert('RGB')
    if background.size != (1920, 1080):
        raise ValueError('This calibration expects the supplied 1920x1080 enclosure photo')
    screen = (711, 174, 504, 750)
    # Reference photo: the finger is roughly 38% of the display width.
    # Scale 1.5 gives approximately that ratio for this 4K hand asset.
    hand_dir = work / 'hand'
    hand_dir.mkdir(exist_ok=True)
    subprocess.run(['ffmpeg', '-v', 'error', '-y', '-i', str(args.hand),
                    '-fps_mode', 'passthrough', str(hand_dir / '%02d.png')], check=True)
    with Image.open(hand_dir / '24.png') as contact:
        if contact.getchannel('A').getextrema()[0] != 0:
            raise ValueError('The hand clip must contain transparency')

    @lru_cache(maxsize=4)
    def hand_frame(pose):
        # Crop transparent margins before scaling; do not keep 40 enlarged
        # 4K canvases in memory. Preserve the crop offset for fingertip alignment.
        with Image.open(hand_dir / f'{pose+1:02d}.png') as source:
            rgba = source.convert('RGBA')
        bounds = rgba.getchannel('A').getbbox()
        if not bounds:
            return None, (0, 0)
        crop = rgba.crop(bounds)
        size = tuple(max(1, round(v * args.hand_scale)) for v in crop.size)
        hand = crop.resize(size, Image.Resampling.LANCZOS)
        offset = (bounds[0]*args.hand_scale, bounds[1]*args.hand_scale)
        if args.hand_treatment == 'original':
            return hand, offset
        # Feather inward: never expose the source clip's grey RGB outside its
        # alpha silhouette. Skin detail and the opaque interior stay sharp.
        alpha = hand.getchannel('A')
        feathered = ImageChops.multiply(alpha, alpha.filter(ImageFilter.GaussianBlur(3.5)))
        hand.putalpha(feathered)
        # Output-pixel measurements keep the effect subtle at 1080p. Padding
        # avoids clipping the blurred shadow along the cropped hand's edges.
        pad = 32
        shadow_alpha = Image.new('L', (hand.width+2*pad, hand.height+2*pad))
        shadow_alpha.paste(feathered, (pad+4, pad+6))
        shadow_alpha = shadow_alpha.filter(ImageFilter.GaussianBlur(7)).point(lambda v: round(v*0.18))
        layer = Image.new('RGBA', shadow_alpha.size, (0, 0, 0, 0))
        layer.putalpha(shadow_alpha)
        layer.alpha_composite(hand, (pad, pad))
        return layer, (offset[0]-pad, offset[1]-pad)
    # Contact point near the fingertip in frame 23 of the original 4K clip.
    anchor = (1928 * args.hand_scale, 448 * args.hand_scale)
    decoder = subprocess.Popen(['ffmpeg', '-v', 'error', '-i', str(native), '-f', 'rawvideo',
                                '-frames:v', str(frame_count), '-pix_fmt', 'rgb24', '-'], stdout=subprocess.PIPE)
    encoder = subprocess.Popen(['ffmpeg', '-v', 'error', '-y', '-f', 'rawvideo', '-pix_fmt', 'rgb24',
                                '-s', '1920x1080', '-r', '30', '-i', '-', '-an', '-c:v', 'libx264',
                                '-crf', '18', '-pix_fmt', 'yuv420p', '-movflags', '+faststart',
                                str(output)], stdin=subprocess.PIPE)
    previews = {0: 'home'}
    for number, (start, end) in enumerate(gestures, 1):
        previews[(start+end)//2] = f'{number:02d}-touch'
        previews[min(len(rows)-1, end+36)] = f'{number:02d}-result'
    for number, (end, start) in enumerate(bridges, 1):
        previews[(end+start)//2] = f'keyboard-{number:02d}-transition'
    try:
        for i, row in enumerate(rows[:frame_count]):
            raw = decoder.stdout.read(640*960*3)
            if len(raw) != 640*960*3:
                raise RuntimeError('Native video ended before touch trace')
            frame = background.copy().convert('RGBA')
            display = Image.frombytes('RGB', (640, 960), raw).resize(screen[2:], Image.Resampling.LANCZOS)
            frame.paste(display, screen[:2])
            # Play the approach, hold the contact pose for the actual drag,
            # then play the release. The trace keeps motion frame-aligned.
            bridge = next(((end, start) for end, start in bridges if end < i < start), None)
            if bridge:
                end, start = bridge
                t = (i-end)/(start-end)
                eased = t*t*(3-2*t)
                x = screen[0] + (int(rows[end]['x'])*(1-eased) + int(rows[start]['x'])*eased)*screen[2]/320
                y = screen[1] + (int(rows[end]['y']) *(1-eased) + int(rows[start]['y'])*eased)*screen[3]/480
                # A visible lift between keys keeps repeated presses from
                # looking like a finger sliding across the glass.
                y -= 52*math.sin(math.pi*t)**2
                hand, offset = hand_frame(23)
                if hand is not None:
                    frame.alpha_composite(hand, (round(x-anchor[0]+offset[0]), round(y-anchor[1]+offset[1])))
            for start, end in (() if bridge else gestures):
                pose = 23 + int((i-start)*25/30) if i < start else 23 if i <= end else 23+int((i-end)*25/30)
                if not 0 <= pose < 40:
                    continue
                touch = rows[start] if i < start else rows[end] if i > end else row
                x = screen[0] + int(touch['x']) * screen[2]/320
                y = screen[1] + int(touch['y']) * screen[3]/480
                hand, offset = hand_frame(pose)
                if hand is not None:
                    frame.alpha_composite(hand, (round(x-anchor[0]+offset[0]), round(y-anchor[1]+offset[1])))
                break
            if i in previews:
                frame.convert('RGB').save(work / f'{previews[i]}.jpg', quality=92)
            encoder.stdin.write(frame.convert('RGB').tobytes())
    finally:
        decoder.stdout.close()
        encoder.stdin.close()
        decoder.wait()
        encoder.wait()
    if decoder.returncode or encoder.returncode:
        raise RuntimeError('Video decoding/encoding failed')
    print(f'Video: {output}\nPreview frames and touch trace: {work}')


if __name__ == '__main__':
    main()
