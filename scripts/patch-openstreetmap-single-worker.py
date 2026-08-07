"""Use one OpenStreetMap tile worker on the original ESP32.

OpenStreetMap-esp32 1.2.2 hard-codes dual-core fetching in a constexpr.
Two simultaneous mbedTLS clients exhaust the Color Kit Grande's contiguous
internal heap, causing three of four map tiles to fail.  Patch the downloaded
dependency before compilation until the library exposes this as a build option.
"""

from pathlib import Path

Import("env")  # type: ignore[name-defined]  # Provided by PlatformIO/SCons.

header = (
    Path(env.subst("$PROJECT_LIBDEPS_DIR"))
    / env.subst("$PIOENV")
    / "OpenStreetMap-esp32"
    / "src"
    / "OpenStreetMap-esp32.hpp"
)

if not header.exists():
    raise RuntimeError(f"OpenStreetMap dependency header not found: {header}")

source = header.read_text()
dual = "constexpr bool OSM_FORCE_SINGLECORE = false;"
single = "constexpr bool OSM_FORCE_SINGLECORE = true;"
if dual in source:
    header.write_text(source.replace(dual, single, 1))
    print("Patched OpenStreetMap-esp32 to use one tile worker")
elif single not in source:
    raise RuntimeError("OpenStreetMap worker configuration changed upstream; update patch script")
