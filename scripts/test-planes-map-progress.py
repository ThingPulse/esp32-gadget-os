#!/usr/bin/env python3
"""After `pio run -e native`, verify map loading animation with slow/failing tiles."""
import os
from pathlib import Path
import shlex
import struct
import subprocess
import sys
import tempfile
import zlib

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
with tempfile.TemporaryDirectory(prefix="planes-map-progress-") as temporary:
    directory = Path(temporary)
    (directory / "data").mkdir()
    # A valid opaque tile, generated without image-library dependencies.
    def chunk(kind, data):
        return struct.pack("!I", len(data)) + kind + data + struct.pack("!I", zlib.crc32(kind + data))
    tile = directory / "tile.png"
    tile.write_bytes(b"\x89PNG\r\n\x1a\n" +
                     chunk(b"IHDR", struct.pack("!2I5B", 256, 256, 8, 2, 0, 0, 0)) +
                     chunk(b"IDAT", zlib.compress((b"\0" + b"\xc0\xd0\xe0" * 256) * 256)) +
                     chunk(b"IEND", b""))
    curl = directory / "curl"
    curl.write_text(f"#!{sys.executable}\n" + '''import os, sys, time
from pathlib import Path
time.sleep(0.2)
if os.getenv("MAP_TEST_FAIL"):
    sys.exit(1)
sys.stdout.buffer.write(Path(os.environ["MAP_TEST_TILE"]).read_bytes())
''')
    curl.chmod(0o755)
    binary = directory / "regression"
    sdl_flags = shlex.split(subprocess.check_output(["sdl2-config", "--libs"], text=True))
    subprocess.run(["c++", "-std=c++14", "-fsanitize=address", "-pthread",
                    "-DLV_CONF_SKIP", "-DLV_FONT_MONTSERRAT_24=1", "-Iinclude",
                    "-I.pio/libdeps/native/lvgl", "test/planes/map_progress_regression.cpp",
                    *map(str, objects), *sdl_flags, "-o", str(binary)], check=True)
    environment = dict(os.environ, PATH=str(directory) + os.pathsep + os.environ["PATH"],
                       MAP_TEST_TILE=str(tile), GADGET_OS_DATA_DIR=str(directory / "data"),
                       ASAN_OPTIONS="detect_leaks=0")
    environment.pop("MAP_TEST_FAIL", None)
    subprocess.run([str(binary)], env=environment, check=True, timeout=30)
