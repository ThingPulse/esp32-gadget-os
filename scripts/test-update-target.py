#!/usr/bin/env python3
"""Compile the actual OTA manifest selector for both hardware variants."""
from pathlib import Path
import os
import subprocess
import tempfile

root = Path(__file__).resolve().parent.parent
with tempfile.TemporaryDirectory(prefix="ota-target-test-") as temporary:
    for pro in (False, True):
        binary = Path(temporary) / ("pro" if pro else "grande")
        subprocess.run([
            os.environ.get("CXX", "c++"), "-std=c++17", "-Wall", "-Wextra", "-Werror",
            *(["-DCOLOR_KIT_PRO=1"] if pro else []),
            "-I", str(root / "include"),
            "-I", str(root / ".pio/libdeps/native/ArduinoJson/src"),
            str(root / "test/update/target_regression.cpp"), "-o", str(binary)
        ], check=True)
        subprocess.run([str(binary)], check=True, timeout=10)
