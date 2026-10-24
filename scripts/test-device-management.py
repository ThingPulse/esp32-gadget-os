#!/usr/bin/env python3
"""Run device check-in policy regressions without SDL or an ESP32 toolchain."""
from pathlib import Path
import os
import runpy
import subprocess
import tempfile
from unittest.mock import patch

root = Path(__file__).resolve().parent.parent
with tempfile.TemporaryDirectory(prefix="device-checkin-test-") as temporary:
    binary = Path(temporary) / "regression"
    subprocess.run([os.environ.get("CXX", "c++"), "-std=c++14", "-Wall", "-Wextra", "-Werror",
                    "-I", str(root / "include"), str(root / "test/device_management/policy_regression.cpp"),
                    "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True, timeout=10)

    class BuildEnvironment:
        def subst(self, value):
            assert value == "$BUILD_DIR"
            return temporary

        def Append(self, **kwargs):
            assert kwargs == {"CPPPATH": [str((Path(temporary) / "generated").resolve())]}

    def generate(key):
        with patch.dict(os.environ, {"GADGET_OS_DEVICE_PRODUCT_KEY": key}):
            runpy.run_path(str(root / "scripts/device-management-config.py"),
                           init_globals={"Import": lambda _: None, "env": BuildEnvironment()})

    header = Path(temporary) / "generated/DeviceManagementBuildConfig.h"
    generate("")
    assert '#define GADGET_OS_DEVICE_PRODUCT_KEY ""' in header.read_text()
    generate("a" * 64)  # Synthetic key, never used on a device or real server.
    assert '"' + "a" * 64 + '"' in header.read_text()
    modified = header.stat().st_mtime_ns
    generate("a" * 64)
    assert header.stat().st_mtime_ns == modified
    for invalid in ["short", "A" * 64, '"; injected', "0" * 65]:
        try:
            generate(invalid)
        except ValueError:
            pass
        else:
            raise AssertionError("Invalid product key accepted")
    generate("")
    assert '"' + "a" * 64 + '"' not in header.read_text()
    print("Device check-in build configuration regressions passed")
