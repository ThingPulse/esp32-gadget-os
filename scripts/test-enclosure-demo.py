#!/usr/bin/env python3
"""Check video language selection and timelines without recording or compositing."""
import contextlib
import importlib.util
import io
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('enclosure_demo', ROOT / 'scripts/create-enclosure-demo.py')
demo = importlib.util.module_from_spec(spec)
previous_bytecode_setting = sys.dont_write_bytecode
sys.dont_write_bytecode = True
try:
    spec.loader.exec_module(demo)
finally:
    sys.dont_write_bytecode = previous_bytecode_setting


class RecordingStarted(Exception):
    pass


class LanguageTests(unittest.TestCase):
    def test_all_scene_languages(self):
        catalogue = json.loads((ROOT / 'translations/de.json').read_text())
        with tempfile.TemporaryDirectory() as directory:
            for language in ('en', 'de'):
                for scene in ('swipe', 'weather', 'planes', 'stocks', 'transit', 'calculator', 'ota'):
                    with self.subTest(language=language, scene=scene):
                        output = Path(directory) / f'{scene}-{language}.mp4'
                        argv = ['demo', '--background', 'unused.png', '--hand', 'unused.mov',
                                '--output', str(output), '--sequence', scene, '--language', language]
                        with patch.object(sys, 'argv', argv), patch.object(demo.subprocess, 'run', side_effect=RecordingStarted) as run:
                            with self.assertRaises(RecordingStarted):
                                demo.main()
                        env = run.call_args.kwargs['env']
                        self.assertEqual(env['GADGET_OS_LANGUAGE'], language)
                        timeline = json.loads(Path(env['GADGET_OS_TUTORIAL_PATH']).read_text())
                        self.assertEqual(timeline['language'], language)
                        actions = timeline['actions']
                        self.assertEqual(actions[0]['route'], 'home')
                        if scene == 'transit':
                            fixture = Path(env['GADGET_OS_TRANSIT_FIXTURE_DIR'])
                            board = json.loads((fixture / 'board.json').read_text())
                            self.assertEqual(len(board['stopTimes']), 4)
                            trip = json.loads((fixture / 'trip.json').read_text())['legs'][0]
                            self.assertEqual(trip['from']['name'], 'Zürich HB')
                            self.assertEqual(trip['to']['name'], 'Winterthur')
                        labels = [a['text'] for a in actions if a['type'] == 'tap_exact']
                        expected = {'weather': ['Weather', 'Close'], 'planes': ['Planes', 'Close'],
                                    'stocks': ['Stocks', '1M', '1Y'],
                                    'calculator': ['Calculator', '4', '*', '.', '='],
                                    'ota': ['Install', 'Close'], 'transit': ['Transit', 'Winterthur'], 'swipe': []}[scene]
                        for label in expected:
                            self.assertIn(catalogue.get(label, label) if language == 'de' else label, labels)

    def test_reuse_requires_matching_language(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            # Legacy files without metadata are English; explicit English is too.
            for metadata in ({'actions': []}, {'language': 'en', 'actions': []}):
                (root / 'tutorial.json').write_text(json.dumps(metadata))
                argv = ['demo', '--background', 'unused.png', '--hand', 'unused.mov',
                        '--output', str(root / 'out.mp4'), '--reuse-recording', str(root), '--language', 'de']
                with patch.object(sys, 'argv', argv), contextlib.redirect_stderr(io.StringIO()) as error:
                    with self.assertRaises(SystemExit) as failure:
                        demo.main()
                self.assertEqual(failure.exception.code, 2)
                self.assertIn('Reused recording is en, not de', error.getvalue())


if __name__ == '__main__':
    unittest.main()
