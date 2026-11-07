"""Build retired regression apps separately from signed release artifacts."""
import pathlib
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]


def build_fixtures(output, apps, chips):
    toolchain = pathlib.Path.home() / '.platformio/packages/toolchain-xtensa-esp-elf/bin'
    for chip in chips:
        if not (toolchain / f'xtensa-{chip}-elf-gcc').is_file():
            raise SystemExit(f'Missing {chip} compiler: build the regular ESP32 firmware profiles first.')
        for app in apps:
            subprocess.run([
                sys.executable, str(ROOT / 'experiments/native-app/build.py'),
                '--chip', chip, '--app', app, '--toolchain', str(toolchain),
                '--output', str(output),
            ], check=True, stdout=subprocess.DEVNULL)
