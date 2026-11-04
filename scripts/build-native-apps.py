#!/usr/bin/env python3
"""Build the independently versioned app set, without rebuilding Gadget OS."""
import argparse,json,pathlib,subprocess,sys
ROOT=pathlib.Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--toolchain',type=pathlib.Path,required=True)
p.add_argument('--signing-key',type=pathlib.Path,required=True)
p.add_argument('--output',type=pathlib.Path,required=True)
a=p.parse_args()
for app in json.loads((ROOT/'experiments/native-app/apps.json').read_text())['apps']:
    for chip in ('esp32','esp32s3'):
        subprocess.run([sys.executable,str(ROOT/'experiments/native-app/build.py'),'--chip',chip,'--app',app['source'],'--id',app['id'],'--version',app['version'],'--toolchain',str(a.toolchain),'--signing-key',str(a.signing_key),'--output',str(a.output)],check=True)
