#!/usr/bin/env python3
"""Validate the firmware snapshot contract independently of ESP32 hardware."""
import pathlib,subprocess,tempfile
root=pathlib.Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory() as temp:
    exe=str(pathlib.Path(temp)/'companion-test')
    subprocess.run(['c++','-std=c++17','-fsanitize=address,undefined','-g','-Iinclude','-I.pio/libdeps/native/ArduinoJson/src','test/native_app/companion.cpp','src/experimental/CompanionData.cpp','src/experimental/CompanionChannels.cpp','src/experimental/CompanionRequest.cpp','-o',exe],cwd=root,check=True)
    subprocess.run([exe],check=True)
