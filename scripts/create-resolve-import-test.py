#!/usr/bin/env python3
"""Create an eight-second OTIO import test and copy its media beside it."""
import json
from pathlib import Path
import shutil
from PIL import Image
import opentimelineio as otio
ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'documents/ads/resolve-import-test'
MEDIA = OUT / 'media'
MEDIA.mkdir(parents=True, exist_ok=True)
Image.new('RGBA', (1920,1080), (0,0,0,0)).save(MEDIA/'title-placeholder.png')
sources = [ROOT / 'documents/user-guide/video/weather-tour-cozy.mp4',
           Path('/Users/deichhor/Downloads/sources/KitComponentsLandscape_GreyBackground.jpg'),
           Path('/Users/deichhor/Downloads/sources/Dance loop 15.wav')]
for source in sources:
    shutil.copy2(source, MEDIA / source.name)
def rt(v): return {'OTIO_SCHEMA': 'RationalTime.1', 'value': v, 'rate': 30}
def tr(start, duration): return {'OTIO_SCHEMA': 'TimeRange.1', 'start_time': rt(start), 'duration': rt(duration)}
def clip(source, duration):
    return {'OTIO_SCHEMA': 'Clip.2', 'name': source.stem, 'source_range': tr(0, duration),
            'active_media_reference_key': 'DEFAULT_MEDIA',
            'media_references': {'DEFAULT_MEDIA': {'OTIO_SCHEMA': 'ExternalReference.1', 'target_url': str(MEDIA/source.name), 'available_range': tr(0, 1 if source.suffix == '.jpg' else 470 if source.suffix == '.mp4' else 914)}},
            'effects': [], 'markers': [], 'metadata': {}}
def track(name, kind, children):
    return {'OTIO_SCHEMA': 'Track.1', 'name': name, 'kind': kind, 'children': children,
            'effects': [], 'markers': [], 'metadata': {}}
timeline = {'OTIO_SCHEMA': 'Timeline.1', 'name': 'ThingPulse - Import Test - 16x9',
            'global_start_time': rt(108000), 'metadata': {},
            'tracks': {'OTIO_SCHEMA': 'Stack.1', 'name': 'tracks', 'children': [
                track('Product shots', 'Video', [clip(sources[0], 120), clip(sources[1], 120)]),
                track('Music', 'Audio', [clip(sources[2], 240)])], 'effects': [], 'markers': [], 'metadata': {}}}
otio.adapters.write_to_file(otio.adapters.read_from_string(json.dumps(timeline), 'otio_json'), str(OUT/'import-test.otio'))
print(OUT/'import-test.otio')
