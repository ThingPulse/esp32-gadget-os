#!/usr/bin/env python3
"""Embed a deterministic gzip page to preserve scarce firmware flash space."""
import gzip
from pathlib import Path
root = Path(__file__).resolve().parent.parent
source = root / 'src/services/HomeAssistantSetup.html'
data = gzip.compress(source.read_bytes(), compresslevel=9, mtime=0)
lines = [', '.join(f'0x{v:02x}' for v in data[i:i+20]) for i in range(0, len(data), 20)]
(root / 'src/services/HomeAssistantSetupPage.inc').write_text(
    '// Generated from HomeAssistantSetup.html by scripts/generate-home-setup.py\n'
    'static const unsigned char SetupPage[] = {\n' + ',\n'.join(lines) + '\n};\n')
print(f'Home setup: {source.stat().st_size} bytes -> {len(data)} bytes gzip')
