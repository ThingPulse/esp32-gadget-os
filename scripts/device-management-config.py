"""Inject the product-scoped ingestion key without putting it in compiler arguments."""
import json
import os
from pathlib import Path
import re

Import("env")

key = os.environ.get("GADGET_OS_DEVICE_PRODUCT_KEY", "")
if key and not re.fullmatch(r"[a-f0-9]{64}", key):
    raise ValueError("GADGET_OS_DEVICE_PRODUCT_KEY must contain exactly 64 lowercase hex characters")

directory = Path(env.subst("$BUILD_DIR")) / "generated"
directory.mkdir(parents=True, exist_ok=True)
header = directory / "DeviceManagementBuildConfig.h"
content = "#pragma once\n#define GADGET_OS_DEVICE_PRODUCT_KEY " + json.dumps(key) + "\n"
if not header.exists() or header.read_text() != content:
    header.write_text(content)
env.Append(CPPPATH=[str(directory.resolve())])
