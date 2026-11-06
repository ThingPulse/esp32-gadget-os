#!/usr/bin/env python3
"""Companion and installable-source tests. Requires native PIO dependencies."""
import argparse, pathlib, subprocess, tempfile
root=pathlib.Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--package-root',type=pathlib.Path,default=root/'.pio/native-app-packages')
a=p.parse_args()

json_include=next(p for p in [root/'.pio/libdeps/native/ArduinoJson/src',root/'.pio/libdeps/esp32-color-kit-native-app/ArduinoJson/src'] if p.exists())
with tempfile.TemporaryDirectory() as temp:
    executable=str(pathlib.Path(temp)/'system-monitor-test')
    subprocess.run(['c++','-std=c++17','-fsanitize=address,undefined','-g','-DGADGET_OS_NATIVE_APP_LOADER=1',
        '-Iinclude','-Iexperiments/native-app/sdk','-I'+str(json_include),
        'test/native_app/system_monitor.cpp','src/experimental/NativeJson.cpp','src/experimental/NativePackage.cpp','src/experimental/NativeSignature.cpp',*subprocess.check_output(['pkg-config','--cflags','--libs','openssl'],text=True).split(),'-o',executable],cwd=root,check=True)
    subprocess.run([executable,*[str(a.package_root/f'{chip}/system-monitor/app.elf') for chip in ('esp32','esp32s3')]],check=True)
