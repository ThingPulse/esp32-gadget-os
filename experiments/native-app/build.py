#!/usr/bin/env python3
"""Build a freestanding target-specific ELF using the installed firmware compiler."""
import argparse, hashlib, json, os, pathlib, subprocess, struct
ROOT = pathlib.Path(__file__).resolve().parents[2]
p = argparse.ArgumentParser()
p.add_argument('--signing-key', type=pathlib.Path, help='ECDSA P-256 private key; omit only for Developer mode')
p.add_argument('--public-key', type=pathlib.Path, default=ROOT/'experiments/native-app/signing-public.pem')
p.add_argument('--chip', required=True, choices=['esp32', 'esp32s3'])
p.add_argument('--id', help='Optional package ID for independent launcher discovery')
p.add_argument('--version', default='0.1.0')
p.add_argument('--app', choices=['counter','iss'], default='counter')
p.add_argument('--toolchain', type=pathlib.Path, required=True, help='PlatformIO toolchain bin directory')
p.add_argument('--output', type=pathlib.Path, default=ROOT / '.pio/native-app-packages')
a = p.parse_args()
# Use the per-chip wrapper: these select the correct Xtensa core configuration.
cc = a.toolchain / f'xtensa-{a.chip}-elf-gcc'
strip = a.toolchain / 'xtensa-esp-elf-strip'
readelf = a.toolchain / 'xtensa-esp-elf-readelf'
app_id = a.id or ('native-iss' if a.app == 'iss' else 'native-demo')
import re
if not re.fullmatch('[a-z][a-z0-9-]{0,31}',app_id): p.error('Invalid app ID')
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
manifest = dict(id=app_id, version=a.version, name='ISS Position' if a.app=='iss' else 'Native Counter', chip=a.chip,
    api_major=1, api_minor=1 if a.app == 'iss' else 0, executable='app.elf')
(out / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
print(subprocess.check_output([str(readelf), '-hSWr', str(elf)], text=True))
print(json.dumps(dict(chip=a.chip, executable_bytes=elf.stat().st_size,
    package_bytes=sum(f.stat().st_size for f in out.iterdir()),
    sha256=hashlib.sha256(elf.read_bytes()).hexdigest(),
    compiler=subprocess.check_output([str(cc), '--version'], text=True).splitlines()[0])))

# A fixed RGB565 icon avoids image codecs and decompression in the installer.
pixels=[]
for y in range(40):
 for x in range(40):
  rgb=(35,126,119) if a.app=='iss' else (133,97,197)
  if a.app=='iss':
   if (2<=x<13 or 27<=x<38) and 8<=y<32: rgb=(99,213,208)
   if (16<=x<24 and 12<=y<28) or (19<=x<21 and 3<=y<12) or (2<=x<38 and 19<=y<21):rgb=(255,255,255)
  elif (18<=x<22 and 8<=y<32) or (8<=x<32 and 18<=y<22):rgb=(255,255,255)
  r,g,b=rgb; pixels.append((r>>3)<<11 | (g>>2)<<5 | b>>3)
icon=struct.pack('<1600H',*pixels)
metadata=json.dumps(manifest,separators=(',',':')).encode(); metadata+=b' '*((-len(metadata))%4)
payload=metadata+icon+elf.read_bytes()
header=b'GOSAPP1\0'+struct.pack('<III',len(metadata),len(icon),elf.stat().st_size)
package=header+hashlib.sha256(header+payload).digest()+payload
if a.signing_key:
 from package_tools import sign_package
 package=sign_package(package,a.signing_key,a.public_key)
path=out.parent/(app_id+'.gapp'); path.write_bytes(package)
print('Installable package:',path,'bytes:',len(package))
