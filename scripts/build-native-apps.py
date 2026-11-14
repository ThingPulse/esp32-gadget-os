#!/usr/bin/env python3
"""Build the independently versioned app set, without rebuilding Gadget OS."""
import argparse
import json
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]


def select_apps(ids=None):
    apps = json.loads((ROOT / 'experiments/native-app/apps.json').read_text())['apps']
    if not ids or ids == ['*']:
        return apps
    unknown = set(ids) - {app['id'] for app in apps}
    if unknown:
        raise ValueError('Unknown app IDs: ' + ', '.join(sorted(unknown)))
    return [app for app in apps if app['id'] in ids]


def build_apps(toolchain, output, chips, apps, signing_key=None):
    for app in apps:
        for chip in chips:
            command = [sys.executable, str(ROOT / 'experiments/native-app/build.py'),
                       '--quiet', '--chip', chip, '--app', app['source'], '--id', app['id'],
                       '--version', app['version'], '--toolchain', str(toolchain),
                       '--output', str(output)]
            if signing_key:
                command += ['--signing-key', str(signing_key)]
            subprocess.run(command, check=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--toolchain', type=Path, required=True)
    parser.add_argument('--signing-key', type=Path, help='Omit for unsigned Developer mode packages')
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--chip', action='append', choices=['esp32', 'esp32s3'])
    parser.add_argument('--app', action='append', help='App ID; repeat to select several (default: all)')
    args = parser.parse_args()
    try:
        apps = select_apps(args.app)
    except ValueError as error:
        parser.error(str(error))
    build_apps(args.toolchain, args.output, args.chip or ['esp32', 'esp32s3'], apps, args.signing_key)


if __name__ == '__main__':
    main()
