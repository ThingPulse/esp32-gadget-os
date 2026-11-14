#!/usr/bin/env python3
"""Run the actual installable phase 3 source against SDL SDK hosts after a native build."""
from pathlib import Path
import os,shlex,subprocess,tempfile,struct,zlib
root=Path(__file__).resolve().parents[1];build=root/'.pio/build/native'
objects=[p for p in (build/'src').rglob('*.o') if p.name not in ('sdl_main.o','sdl_main.cpp.o')]
for lib in build.glob('lib*'):objects.extend(list(lib.glob('*.a')) or list(lib.rglob('*.o')))
out=root/'.pio/ui-previews';out.mkdir(parents=True,exist_ok=True)
with tempfile.TemporaryDirectory() as temp:
 binary=Path(temp)/'phase3-test'
 subprocess.run(['c++','-std=c++17','-fsanitize=address','-pthread','-DLV_CONF_SKIP','-DLV_USE_QRCODE=1','-DLV_USE_SYSMON=1','-DLV_USE_PERF_MONITOR=1','-DLV_FONT_MONTSERRAT_32=1','-DLV_FONT_MONTSERRAT_24=1','-DGADGET_OS_APP_STORE_SIMULATOR=1','-Iinclude','-I.pio/libdeps/native/lvgl','-I.pio/libdeps/native/ArduinoJson/src','test/native_app/phase3.cpp',*map(str,objects),*shlex.split(subprocess.check_output(['sdl2-config','--libs'],text=True)),'-o',str(binary)],cwd=root,check=True)
 subprocess.run([str(binary),str(out)],cwd=temp,env=dict(os.environ,ASAN_OPTIONS='detect_leaks=0',GADGET_OS_DATA_DIR=temp),check=True)
for p in out.glob('phase3-*.ppm'):
 raw=p.read_bytes().split(b'\n',3)[3]
 def chunk(k,d):return struct.pack('!I',len(d))+k+d+struct.pack('!I',zlib.crc32(k+d)&0xffffffff)
 p.with_suffix('.png').write_bytes(b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('!2I5B',320,480,8,2,0,0,0))+chunk(b'IDAT',zlib.compress(b''.join(b'\0'+raw[y*960:(y+1)*960] for y in range(480))))+chunk(b'IEND',b''))
