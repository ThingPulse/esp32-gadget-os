#!/usr/bin/env python3
"""Report built artifact sizes and prove disabled ELFs contain no loader symbols."""
import argparse, json, pathlib, subprocess
p=argparse.ArgumentParser();p.add_argument('--toolchain',type=pathlib.Path,required=True);a=p.parse_args()
root=pathlib.Path(__file__).resolve().parents[2]
rows=[]
for env in ['esp32-color-kit','esp32-color-kit-pro','esp32-color-kit-native-app','esp32-color-kit-pro-native-app']:
    folder=root/'.pio/build'/env
    symbols=subprocess.check_output([str(a.toolchain/'xtensa-esp-elf-nm'),'-C',str(folder/'firmware.elf')],text=True)
    present='native_app::poll()' in symbols and 'esp_elf_relocate' in symbols
    experimental=env.endswith('native-app')
    assert present==experimental,(env,'loader symbol mismatch')
    if not experimental: assert 'native_app::' not in symbols and 'esp_elf_' not in symbols
    rows.append(dict(environment=env,firmware_bin_bytes=(folder/'firmware.bin').stat().st_size,loader_symbols=present))
for chip in ['esp32','esp32s3']:
    folder=root/'.pio/native-app-packages'/chip/'native-demo'
    rows.append(dict(chip=chip,elf_bytes=(folder/'app.elf').stat().st_size,
        package_bytes=sum(p.stat().st_size for p in folder.iterdir() if p.is_file())))
print(json.dumps(rows,indent=2))
