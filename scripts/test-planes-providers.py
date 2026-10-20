#!/usr/bin/env python3
"""Run after `pio run -e native`; optionally pass a saved live JSON response."""
import glob
import os
from pathlib import Path
import subprocess
import sys
import tempfile

root = Path(__file__).resolve().parent.parent
os.chdir(root)
with tempfile.TemporaryDirectory(prefix="planes-test-") as temporary:
    directory = Path(temporary)
    allocator_test = directory / "tls-memory-regression"
    subprocess.run([
        "c++", "-std=c++14", "-fsanitize=address",
        "-DARDUINO", "-DESP_PLATFORM", "-DBOARD_HAS_PSRAM",
        "-Itest/tls_memory/stubs", "-Iinclude",
        "src/services/TlsMemory.cpp", "test/tls_memory/allocator_regression.cpp",
        "-o", str(allocator_test),
    ], check=True)
    subprocess.run([str(allocator_test)], check=True)
    curl = directory / "curl"
    curl.write_text(f"#!{sys.executable}\n" + '''import json, os, sys
url = sys.argv[-1]
code = 200
if os.getenv("PLANES_TEST_FAIL"):
    body = {"error": "unavailable"}
elif os.getenv("PLANES_TEST_EMPTY"):
    body = {"ac": []}
elif "api.adsb.lol" in url:
    body = {"error": "unavailable"}
elif "api.airplanes.live" in url:
    body = {"error": "forbidden"}
    code = 403
elif "opendata.adsb.fi" in url:
    body = {"aircraft": [{"hex": "4b1234", "lat": 47.43, "lon": 8.55}]}
else:
    raise SystemExit("Unexpected provider: " + url)
print(json.dumps(body))
print("__COPILOT_HTTP_CODE:" + str(code))
''')
    curl.chmod(0o755)
    names = {"ServiceRegistry", "EventBus", "PlanesService", "ConfigStoreService",
             "FileSystemService", "WifiService"}
    objects = [p for p in glob.glob(".pio/build/native/src/**/*.o", recursive=True)
               if Path(p).name.removesuffix(".o").removesuffix(".cpp") in names]
    if len(objects) != len(names):
        raise SystemExit("Build native first: pio run -e native")
    binary = directory / "provider-regression"
    subprocess.run(["c++", "-std=c++14", "-fsanitize=address", "-pthread", "-Iinclude",
                    "test/planes/provider_regression.cpp", *objects,
                    "-o", str(binary)], check=True)
    environment = dict(os.environ, PATH=str(directory) + os.pathsep + os.environ["PATH"],
                       ASAN_OPTIONS="detect_leaks=0")
    subprocess.run([str(binary), *sys.argv[1:]], env=environment, check=True)
