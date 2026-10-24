#!/usr/bin/env python3
"""Exercise OTA scheduling without a device or elapsed wall-clock waits."""
from pathlib import Path
import os
import subprocess
import tempfile

root = Path(__file__).resolve().parent.parent
with tempfile.TemporaryDirectory(prefix="ota-schedule-test-") as temporary:
    binary = Path(temporary) / "regression"
    subprocess.run([
        os.environ.get("CXX", "c++"), "-std=c++14", "-Wall", "-Wextra", "-Werror",
        "-I", str(root / "include"), str(root / "test/update/schedule_regression.cpp"),
        "-o", str(binary),
    ], check=True)
    subprocess.run([str(binary)], check=True, timeout=10)
