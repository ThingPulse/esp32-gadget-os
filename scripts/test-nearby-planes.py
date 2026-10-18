#!/usr/bin/env python3
"""Run nearby aircraft regression checks after `pio run -e native`."""
import os
from pathlib import Path
import shlex
import subprocess
import tempfile

root = Path(__file__).resolve().parent.parent
os.chdir(root)
build = root / ".pio/build/native"
objects = [p for p in (build / "src").rglob("*.o")
           if p.name not in {"PlanesApp.o", "PlanesApp.cpp.o", "sdl_main.o", "sdl_main.cpp.o"}]
if not objects:
    raise SystemExit("Build native first: pio run -e native")
for library in build.glob("lib*"):
    archives = list(library.glob("*.a"))
    objects.extend(archives or list(library.rglob("*.o")))
with tempfile.TemporaryDirectory(prefix="nearby-planes-test-") as temporary:
    binary = Path(temporary) / "regression"
    sdl_flags = shlex.split(subprocess.check_output(["sdl2-config", "--libs"], text=True))
    subprocess.run(["c++", "-std=c++14", "-fsanitize=address", "-pthread",
                    "-DLV_CONF_SKIP", "-DLV_USE_SYSMON=0", "-DLV_FONT_MONTSERRAT_24=1",
                    "-Iinclude", "-I.pio/libdeps/native/lvgl", "test/planes/nearby_regression.cpp",
                    *map(str, objects), *sdl_flags, "-o", str(binary)], check=True)
    subprocess.run([str(binary)], env=dict(os.environ, ASAN_OPTIONS="detect_leaks=0",
                                         GADGET_OS_DATA_DIR=temporary),
                   check=True, timeout=30)
