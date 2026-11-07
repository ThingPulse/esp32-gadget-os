#!/usr/bin/env python3
"""Exercise the on-device catalog parser with sanitizers."""
import pathlib,subprocess,tempfile
root=pathlib.Path(__file__).resolve().parents[1]
crypto=subprocess.check_output(['pkg-config','--cflags','--libs','openssl'],text=True).split()
with tempfile.TemporaryDirectory() as tmp:
 binary=pathlib.Path(tmp)/'catalog'
 subprocess.run(['c++','-std=c++17','-g','-fsanitize=address,undefined','-fno-omit-frame-pointer','-DGADGET_OS_NATIVE_APP_LOADER=1','-Iinclude','-I.pio/libdeps/esp32-color-kit/ArduinoJson/src','src/experimental/NativeCatalog.cpp','src/experimental/NativePackage.cpp','src/experimental/NativeSignature.cpp','test/native_app/catalog.cpp',*crypto,'-o',str(binary)],cwd=root,check=True)
 subprocess.run([str(binary)],check=True)
