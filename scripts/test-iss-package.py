#!/usr/bin/env python3
"""Validate both ISS target packages with the actual firmware package/ELF parser."""
from pathlib import Path
import subprocess,tempfile
root=Path(__file__).resolve().parents[1]
json_include=next(p for p in [root/'.pio/libdeps/native/ArduinoJson/src',root/'.pio/libdeps/esp32-color-kit/ArduinoJson/src'] if p.exists())
crypto=subprocess.check_output(['pkg-config','--cflags','--libs','openssl'],text=True).split()
with tempfile.TemporaryDirectory() as temp:
 binary=Path(temp)/'iss-package-test'
 subprocess.run(['c++','-std=c++17','-fsanitize=address,undefined','-DGADGET_OS_NATIVE_APP_LOADER=1','-Iinclude','-I'+str(json_include),'test/native_app/iss_package.cpp','src/experimental/NativePackage.cpp','src/experimental/NativeSignature.cpp',*crypto,'-o',str(binary)],cwd=root,check=True)
 subprocess.run([str(binary),*[str(root/'.pio/native-app-packages'/chip/'iss-tracker.gapp') for chip in ('esp32','esp32s3')]],check=True)
