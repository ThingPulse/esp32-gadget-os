#!/usr/bin/env python3
"""Real package validator under sanitizers; fixtures are independently packed."""
import pathlib,subprocess,tempfile,json,struct,hashlib
from native_test_fixtures import build_fixtures
root=pathlib.Path(__file__).resolve().parents[1]
arduino_json=root/'.pio/libdeps/esp32-color-kit/ArduinoJson/src'
if not arduino_json.exists():arduino_json=root/'.pio/libdeps/esp32-color-kit/ArduinoJson/src'
with tempfile.TemporaryDirectory() as source:
 build_fixtures(pathlib.Path(source), ('iss',), ('esp32s3',))
 elf=(pathlib.Path(source)/'esp32s3/native-iss/app.elf').read_bytes()
 base=dict(id='independent-app',name='Independent App',version='0.1.0',chip='esp32s3',api_major=1,api_minor=1,executable='app.elf')
 def pack(meta,code=elf,icon=b'\0'*3200):
  m=json.dumps(meta).encode();m+=b' '*(-len(m)%4)
  payload=m+icon+code;header=b'GOSAPP1\0'+struct.pack('<III',len(m),len(icon),len(code))
  return header+hashlib.sha256(header+payload).digest()+payload
 with tempfile.TemporaryDirectory() as folder:
  folder=pathlib.Path(folder);fixtures={'ok-generic':pack(base),'bad-elf':pack(base,b'x'*100),'bad-icon':pack(base,icon=b'x')}
  for key,value in [('id','../escape'),('id','a/b'),('id','A'),('id','a'*33),('name',''),('name','n'*33),('version','v'*33),('chip','esp32'),('api_major',2),('api_minor',11),('api_minor',-1),('executable','../app.elf')]:
   fixtures[f'bad-{len(fixtures)}']=pack(dict(base,**{key:value}))
  fixtures['bad-extra']=pack(base)+b'x'
  for name,data in fixtures.items():(folder/name).write_bytes(data)
  binary=folder/'test'
  subprocess.run(['c++','-std=c++17','-g','-fsanitize=address,undefined','-fno-omit-frame-pointer','-DGADGET_OS_NATIVE_APP_LOADER=1','-Iinclude','-I'+str(arduino_json),'src/experimental/NativePackage.cpp','src/experimental/NativeSignature.cpp',*subprocess.check_output(['pkg-config','--cflags','--libs','openssl'],text=True).split(),'test/native_app/package.cpp','-o',str(binary)],cwd=root,check=True)
  subprocess.run([str(binary),*[str(folder/name) for name in fixtures]],check=True)
