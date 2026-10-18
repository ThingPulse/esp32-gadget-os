#!/usr/bin/env python3
"""Regenerate the embedded RGB888 moon texture (requires Pillow)."""
from pathlib import Path
from PIL import Image

root = Path(__file__).resolve().parent.parent
image = Image.open(root / "assets/weather/moon-full-72.png").convert("RGB")
if image.size != (72, 72):
    raise SystemExit("Expected a 72 x 72 moon texture")
pixels = image.tobytes()
with (root / "include/assets/MoonTextureData.inc").open("w") as output:
    output.write("// RGB888, 72 x 72. Generated from assets/weather/moon-full-72.png.\n")
    output.write("static const uint8_t moon_texture[] = {\n")
    for offset in range(0, len(pixels), 24):
        output.write("    " + ",".join(map(str, pixels[offset:offset + 24])) + ",\n")
    output.write("};\n")
