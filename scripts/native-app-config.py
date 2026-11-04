"""Configure experiment flags before PlatformIO clones source environments."""
from pathlib import Path
Import("env")
root = Path(env.subst('$PROJECT_DIR'))
vendor = root / 'experiments/native-app/vendor/elf_loader'
env.Append(CPPPATH=[str(vendor / 'include'), str(root / 'experiments/native-app/sdk')])
env.Append(CPPDEFINES=[('CONFIG_ELF_LOADER', 1), ('CONFIG_ELF_LOADER_BUS_ADDRESS_MIRROR', 1),
    ('CONFIG_ELF_LOADER_NUMBER_SYMBOLS', 1), ('ELF_LOADER_VER_MAJOR', 1),
    ('ELF_LOADER_VER_MINOR', 3), ('ELF_LOADER_VER_PATCH', 4)])
