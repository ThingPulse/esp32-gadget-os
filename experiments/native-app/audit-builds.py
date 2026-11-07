#!/usr/bin/env python3
"""Report built artifact sizes and verify regular firmware includes the app loader."""
import argparse, json, pathlib, subprocess
p=argparse.ArgumentParser();p.add_argument('--toolchain',type=pathlib.Path,required=True);a=p.parse_args()
root=pathlib.Path(__file__).resolve().parents[2]
rows=[]
for env in ['esp32-color-kit','esp32-color-kit-pro']:
    folder=root/'.pio/build'/env
    symbols=subprocess.check_output([str(a.toolchain/'xtensa-esp-elf-nm'),'-C',str(folder/'firmware.elf')],text=True)
    present='native_app::poll()' in symbols and 'esp_elf_relocate' in symbols
    assert present,(env,'missing app loader symbols')
    rows.append(dict(environment=env,firmware_bin_bytes=(folder/'firmware.bin').stat().st_size,loader_symbols=present))
for chip in ['esp32','esp32s3']:
    for app in ['native-demo','native-iss']:
        folder=root/'.pio/native-app-packages'/chip/app
        rows.append(dict(chip=chip,app=app,elf_bytes=(folder/'app.elf').stat().st_size,
            package_bytes=sum(p.stat().st_size for p in folder.iterdir() if p.is_file()),
            installable_bytes=folder.with_suffix('.gapp').stat().st_size))
print(json.dumps(rows,indent=2))
