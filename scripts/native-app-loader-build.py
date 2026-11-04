"""Build pinned loader as a library after the Arduino toolchain is configured."""
from pathlib import Path
Import("env")
vendor = Path(env.subst('$PROJECT_DIR')) / 'experiments/native-app/vendor/elf_loader'
# No PSRAM execution or libc/IDF symbol tables. Do not modify the prebuilt sdkconfig.
env.Append(LIBS=[env.BuildLibrary('$BUILD_DIR/elf_loader', str(vendor / 'src'),
    src_filter=['+<esp_elf.c>', '+<esp_elf_adapter.c>', '+<arch/esp_elf_xtensa.c>'])])
