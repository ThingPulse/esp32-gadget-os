#!/usr/bin/env python3
"""Run portable parser/gate/demo tests; not a substitute for device cycles."""
import os, pathlib, subprocess, tempfile
from native_test_fixtures import build_fixtures
root=pathlib.Path(__file__).resolve().parents[1]
os.chdir(root)
arduino_json=root/'.pio/libdeps/esp32-color-kit/ArduinoJson/src'
if not arduino_json.exists(): arduino_json=root/'.pio/libdeps/esp32-color-kit-native-app/ArduinoJson/src'
if not arduino_json.exists(): raise SystemExit('Install the experimental PlatformIO dependencies first')
with tempfile.TemporaryDirectory() as temp:
    temp=pathlib.Path(temp)
    fixtures=temp/'fixtures'
    build_fixtures(fixtures, ('counter','iss'), ('esp32','esp32s3'))
    flags=['-g','-fsanitize=address,undefined','-fno-omit-frame-pointer']
    subprocess.run(['cc','-shared','-fPIC',*flags,'-Iexperiments/native-app/sdk',
        'experiments/native-app/example/counter.c','-o',str(temp/'demo.so')],check=True)
    subprocess.run(['c++','-std=c++17',*flags,'-DGADGET_OS_NATIVE_APP_LOADER=1',
        '-Iinclude','-Iexperiments/native-app/sdk','-I'+str(arduino_json),
        'src/experimental/NativePackage.cpp','src/experimental/NativeSignature.cpp',*subprocess.check_output(['pkg-config','--cflags','--libs','openssl'],text=True).split(),'test/native_app/regression.cpp','-o',str(temp/'test')],check=True)
    subprocess.run([str(temp/'test'),str(fixtures/'esp32/native-demo/app.elf'),
        str(fixtures/'esp32s3/native-demo/app.elf'),str(temp/'demo.so')],check=True)
    subprocess.run(['cc','-shared','-fPIC',*flags,'-Iexperiments/native-app/sdk',
        'experiments/native-app/example/iss.c','-o',str(temp/'iss.so')],check=True)
    subprocess.run(['c++','-std=c++17',*flags,'-DGADGET_OS_NATIVE_APP_LOADER=1',
        '-Iinclude','-Iexperiments/native-app/sdk','-I'+str(arduino_json),
        'src/experimental/NativePackage.cpp','src/experimental/NativeSignature.cpp',*subprocess.check_output(['pkg-config','--cflags','--libs','openssl'],text=True).split(),'src/experimental/NativeJson.cpp',
        'test/native_app/http.cpp','-o',str(temp/'http-test')],check=True)
    subprocess.run([str(temp/'http-test'),str(temp/'iss.so'),
        str(fixtures/'esp32/native-iss/app.elf'),
        str(fixtures/'esp32s3/native-iss/app.elf')],check=True)
