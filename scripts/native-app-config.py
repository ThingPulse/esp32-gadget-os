"""Configure experiment flags before PlatformIO clones source environments."""
from pathlib import Path
Import("env")
root = Path(env.subst('$PROJECT_DIR'))
vendor = root / 'experiments/native-app/vendor/elf_loader'
env.Append(CPPPATH=[str(vendor / 'include'), str(root / 'experiments/native-app/sdk')])
env.Append(CPPDEFINES=[('CONFIG_ELF_LOADER', 1), ('CONFIG_ELF_LOADER_BUS_ADDRESS_MIRROR', 1),
    ('CONFIG_ELF_LOADER_NUMBER_SYMBOLS', 1), ('ELF_LOADER_VER_MAJOR', 1),
    ('ELF_LOADER_VER_MINOR', 3), ('ELF_LOADER_VER_PATCH', 4)])

# The private key never enters the firmware or compiler command line.
import json, os
from urllib.parse import urlparse
public_path=Path(os.environ.get('GADGET_OS_APP_PUBLIC_KEY_FILE',root/'experiments/native-app/signing-public.pem'))
public=public_path.read_text()
catalog=os.environ.get('GADGET_OS_APP_CATALOG_URL','https://thingpulse.github.io/esp32-gadget-os/apps/catalog.json')
url=urlparse(catalog)
if url.scheme!='https' or not url.hostname or url.username or url.password or url.fragment:
    raise ValueError('App catalog must be an HTTPS URL without credentials or fragment')
generated=Path(env.subst('$BUILD_DIR'))/'generated'
generated.mkdir(parents=True,exist_ok=True)
header=generated/'NativeTrust.h'
content='#pragma once\n#define GADGET_OS_APP_PUBLIC_KEY '+json.dumps(public)+'\n#define GADGET_OS_APP_CATALOG_URL '+json.dumps(catalog)+'\n#define GADGET_OS_APP_CATALOG_ORIGIN '+json.dumps('https://'+url.netloc)+'\n'
if not header.exists() or header.read_text()!=content:header.write_text(content)
env.Append(CPPPATH=[str(generated.resolve())])
