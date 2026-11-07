#!/usr/bin/env python3
"""Exercise the shared store UI and persistent offline simulator installations."""
from pathlib import Path
import os, shlex, subprocess, tempfile
root=Path(__file__).resolve().parents[1]
build=root/Path(os.environ.get('PLATFORMIO_BUILD_DIR','.pio/build'))/'native'
if not (build/'program').exists():
    raise SystemExit('Build the native environment before running this test.')
objects=[p for p in (build/'src').rglob('*.o') if p.name not in ('sdl_main.o','sdl_main.cpp.o','user_code.o','user_code.cpp.o')]
for lib in build.glob('lib*'):
    objects.extend(list(lib.glob('*.a')) or list(lib.rglob('*.o')))
output=root/'.pio/store-previews';output.mkdir(parents=True,exist_ok=True)
with tempfile.TemporaryDirectory() as temp:
    binary=Path(temp)/'store-test'
    subprocess.run(['c++','-std=c++17','-fsanitize=address','-pthread','-DLV_CONF_SKIP','-DLV_USE_SYSMON=1','-DLV_USE_PERF_MONITOR=1','-DLV_FONT_MONTSERRAT_32=1','-DLV_FONT_MONTSERRAT_24=1','-Iinclude','-I.pio/libdeps/native/lvgl',
        '-DGADGET_OS_APP_STORE_SIMULATOR=1','-I.pio/libdeps/native/ArduinoJson/src','test/native_app/store_simulator.cpp',*map(str,objects),*shlex.split(subprocess.check_output(['sdl2-config','--libs'],text=True)),'-o',str(binary)],cwd=root,check=True)
    env=dict(os.environ,ASAN_OPTIONS='detect_leaks=0');env.pop('GADGET_USAGE_SNAPSHOT',None);env.pop('GADGET_SYSTEM_SNAPSHOT',None)
    for phase in ('fresh','restart'):
        subprocess.run([str(binary),str(output),phase],cwd=temp,env=env,check=True)
    # Convert the rendered framebuffer to PNG without optional Python packages.
    import struct,zlib
    for ppm in output.glob('*.ppm'):
        raw=ppm.read_bytes().split(b'\n',3)[3]
        def chunk(name,data):return struct.pack('!I',len(data))+name+data+struct.pack('!I',zlib.crc32(name+data)&0xffffffff)
        rows=b''.join(b'\x00'+raw[y*960:(y+1)*960] for y in range(480))
        ppm.with_suffix('.png').write_bytes(b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('!2I5B',320,480,8,2,0,0,0))+chunk(b'IDAT',zlib.compress(rows))+chunk(b'IEND',b''))
print('Screenshots:',output)
