#!/usr/bin/env python3
"""Exercise unified launcher and API 1.8 settings with the real native UI."""
import os, pathlib, shlex, subprocess, tempfile
root=pathlib.Path(__file__).resolve().parents[1]
build=root/'.pio/build/native'
objects=[p for p in (build/'src').rglob('*.o') if p.name not in ('sdl_main.o','sdl_main.cpp.o')]
for lib in build.glob('lib*'):
    objects.extend(list(lib.glob('*.a')) or list(lib.rglob('*.o')))
with tempfile.TemporaryDirectory(prefix='launcher-settings-') as temporary:
    binary=pathlib.Path(temporary)/'test'
    subprocess.run(['c++','-std=c++17','-fsanitize=address','-pthread',
        '-DGADGET_OS_APP_STORE_SIMULATOR=1','-DLV_CONF_SKIP','-DLV_USE_SYSMON=1','-DLV_USE_PERF_MONITOR=1',
        '-DLV_FONT_MONTSERRAT_24=1','-DLV_FONT_MONTSERRAT_32=1',
        '-Iinclude','-I.pio/libdeps/native/lvgl','-I.pio/libdeps/native/ArduinoJson/src','-Iexperiments/native-app/sdk',
        'test/native_app/launcher.cpp',*map(str,objects),*shlex.split(subprocess.check_output(['sdl2-config','--libs'],text=True)),
        '-o',str(binary)],cwd=root,check=True)
    subprocess.run([str(binary)],cwd=temporary,env=dict(os.environ,ASAN_OPTIONS='detect_leaks=0',GADGET_OS_DATA_DIR=temporary),check=True,timeout=30)
