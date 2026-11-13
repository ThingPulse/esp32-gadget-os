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
p.add_argument('--app', choices=['stocks','settings_demo','counter','iss','agent_usage','system_monitor','iss_tracker'], default='counter')
p.add_argument('--toolchain', type=pathlib.Path, required=True, help='PlatformIO toolchain bin directory')
p.add_argument('--output', type=pathlib.Path, default=ROOT / '.pio/native-app-packages')
p.add_argument('--usage-url', help='Private HTTPS companion URL for agent_usage')
p.add_argument('--usage-url-file', type=pathlib.Path, help='Read private HTTPS URL from file')
p.add_argument('--usage-demo', action='store_true', help='Clearly labelled offline sample data')
a = p.parse_args()
if a.usage_url_file:
 if a.usage_url: p.error('Choose --usage-url or --usage-url-file')
 a.usage_url=a.usage_url_file.read_text().strip()
extra_flags=[]
if a.app=='agent_usage':
 if a.usage_demo and a.usage_url: p.error('Choose demo or live URL')
 if a.usage_url:
  from urllib.parse import urlsplit
  url=urlsplit(a.usage_url)
  if url.scheme!='https' or not url.hostname or url.username or url.password or url.fragment or len(a.usage_url)>255 or any(ord(c)<33 or ord(c)>126 for c in a.usage_url): p.error('Expected HTTPS URL <=255 bytes, without credentials or fragment')
  extra_flags.append('-DAGENT_USAGE_URL='+json.dumps(a.usage_url))
 if a.usage_demo: extra_flags.append('-DAGENT_USAGE_DEMO=1')
# Use the per-chip wrapper: these select the correct Xtensa core configuration.
cc = a.toolchain / f'xtensa-{a.chip}-elf-gcc'
strip = a.toolchain / 'xtensa-esp-elf-strip'
readelf = a.toolchain / 'xtensa-esp-elf-readelf'
app_id = a.id or {'stocks':'stocks','settings_demo':'settings-demo','counter':'native-demo','iss':'native-iss','iss_tracker':'iss-tracker','system_monitor':'system-monitor','agent_usage':'agent-usage'}[a.app]
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
    *extra_flags, str(ROOT / f'experiments/native-app/example/{a.app}.c'), '-o', str(elf)], check=True)
subprocess.run([str(strip), '--strip-unneeded', '--remove-section=.comment',
    '--remove-section=.got.loc', '--remove-section=.dynamic', '--remove-section=.xt.lit',
    '--remove-section=.xt.prop', '--remove-section=.xtensa.info', str(elf)], check=True)
manifest = dict(id=app_id, version=a.version, name={'stocks':'Stocks','settings_demo':'Settings Demo','counter':'Native Counter','iss':'ISS Position','iss_tracker':'ISS','system_monitor':'System Monitor','agent_usage':'Agent Dashboard Demo' if a.usage_demo else 'Agent Dashboard'}[a.app], chip=a.chip,
    api_major=1, api_minor={'stocks':9,'settings_demo':8,'counter':0,'iss':1,'iss_tracker':5,'agent_usage':7,'system_monitor':7}[a.app], executable='app.elf')
if a.app in ('settings_demo','stocks'): manifest['settings']=True
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
  rgb=(35,126,119) if a.app in ('iss','iss_tracker') else (133,97,197)
  if a.app=='system_monitor':
   rgb=(23,33,49)
   if (5<=x<35 and (y in (7,8,29,30))) or (y>=7 and y<=30 and x in (5,6,33,34)) or (18<=x<=21 and 31<=y<35) or (12<=x<=27 and y==35): rgb=(99,230,190)
   if 9<=x<=30 and abs(y-(20 if x<15 else 13 if x<20 else 24 if x<25 else 18))<2: rgb=(99,230,190)
  elif a.app=='agent_usage':
   rgb=(23,33,49)
   if (7<=x<13 and 24<=y<34) or (17<=x<23 and 16<=y<34) or (27<=x<33 and 6<=y<34): rgb=(99,230,190)
  elif a.app in ('iss','iss_tracker'):
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
