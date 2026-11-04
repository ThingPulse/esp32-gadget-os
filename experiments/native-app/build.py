#!/usr/bin/env python3
"""Build a freestanding target-specific ELF using the installed firmware compiler."""
import argparse, hashlib, json, os, pathlib, subprocess
ROOT = pathlib.Path(__file__).resolve().parents[2]
p = argparse.ArgumentParser()
p.add_argument('--chip', required=True, choices=['esp32', 'esp32s3'])
p.add_argument('--app', choices=['counter','iss'], default='counter')
p.add_argument('--toolchain', type=pathlib.Path, required=True, help='PlatformIO toolchain bin directory')
p.add_argument('--output', type=pathlib.Path, default=ROOT / '.pio/native-app-packages')
a = p.parse_args()
# Use the per-chip wrapper: these select the correct Xtensa core configuration.
cc = a.toolchain / f'xtensa-{a.chip}-elf-gcc'
strip = a.toolchain / 'xtensa-esp-elf-strip'
readelf = a.toolchain / 'xtensa-esp-elf-readelf'
app_id = 'native-iss' if a.app == 'iss' else 'native-demo'
out = a.output / a.chip / app_id
out.mkdir(parents=True, exist_ok=True)
elf = out / 'app.elf'
subprocess.run([str(cc), '-Os', '-fPIC', '-fvisibility=hidden', '-ffreestanding',
    '-fno-builtin', '-fno-stack-protector', '-ffunction-sections', '-fdata-sections',
    '-nostdlib', '-nostartfiles', '-shared', '-Wl,-Bsymbolic', '-Wl,--no-undefined',
    '-Wl,--gc-sections', '-Wl,-e,gadget_app_entry',
    '-Wl,-T,' + str(ROOT / 'experiments/native-app/example/app.ld'),
    '-I' + str(ROOT / 'experiments/native-app/sdk'),
    str(ROOT / f'experiments/native-app/example/{a.app}.c'), '-o', str(elf)], check=True)
subprocess.run([str(strip), '--strip-unneeded', '--remove-section=.comment',
    '--remove-section=.got.loc', '--remove-section=.dynamic', '--remove-section=.xt.lit',
    '--remove-section=.xt.prop', '--remove-section=.xtensa.info', str(elf)], check=True)
manifest = dict(id=app_id, version='0.1.0', chip=a.chip,
    api_major=1, api_minor=1 if a.app == 'iss' else 0, executable='app.elf')
(out / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
print(subprocess.check_output([str(readelf), '-hSWr', str(elf)], text=True))
print(json.dumps(dict(chip=a.chip, executable_bytes=elf.stat().st_size,
    package_bytes=sum(f.stat().st_size for f in out.iterdir()),
    sha256=hashlib.sha256(elf.read_bytes()).hexdigest(),
    compiler=subprocess.check_output([str(cc), '--version'], text=True).splitlines()[0])))
