#!/usr/bin/env python3
"""Exercise real firmware verification with disposable keys, never release secrets."""
import hashlib,importlib.util,json,pathlib,struct,subprocess,sys,tempfile
ROOT=pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'experiments/native-app'))
from package_tools import sign_package,public_key
spec=importlib.util.spec_from_file_location('catalog',ROOT/'scripts/build-native-catalog.py')
catalog=importlib.util.module_from_spec(spec);spec.loader.exec_module(catalog)
with tempfile.TemporaryDirectory() as folder:
    temp=pathlib.Path(folder)
    toolchain=pathlib.Path.home()/'.platformio/packages/toolchain-xtensa-esp-elf/bin'
    subprocess.run([sys.executable,str(ROOT/'experiments/native-app/build.py'),'--chip','esp32s3','--app','counter','--toolchain',str(toolchain),'--output',str(temp/'unsigned')],check=True,stdout=subprocess.DEVNULL)
    unsigned=(temp/'unsigned/esp32s3/native-demo.gapp').read_bytes()
    keys=[]
    for name in ('trusted','other'):
        key=temp/(name+'.pem');pub=temp/(name+'.pub')
        subprocess.run(['openssl','genpkey','-algorithm','EC','-pkeyopt','ec_paramgen_curve:P-256','-out',str(key)],check=True,stdout=subprocess.DEVNULL,stderr=subprocess.PIPE)
        pub.write_bytes(public_key(key));keys.append((key,pub))
    signed=sign_package(unsigned,*keys[0])
    fixtures={'ok-signed':signed,'bad-untrusted':sign_package(unsigned,*keys[1])}
    modified=bytearray(signed);modified[-1]^=1;modified[20:52]=hashlib.sha256(modified[:20]+modified[52:60]+modified[124:]).digest();fixtures['bad-rehashed']=modified
    invalid=bytearray(unsigned);invalid[-100:]=b'x'*100;invalid[20:52]=hashlib.sha256(invalid[:20]+invalid[52:]).digest();fixtures['bad-signed-elf']=sign_package(invalid,*keys[0])
    for name,data in fixtures.items():(temp/name).write_bytes(data)
    binary=temp/'verify';crypto=subprocess.check_output(['pkg-config','--cflags','--libs','openssl'],text=True).split()
    subprocess.run(['c++','-std=c++17','-g','-fsanitize=address,undefined','-fno-omit-frame-pointer','-DGADGET_OS_NATIVE_APP_LOADER=1','-Iinclude','-I.pio/libdeps/esp32-color-kit-native-app/ArduinoJson/src','src/experimental/NativePackage.cpp','src/experimental/NativeSignature.cpp','test/native_app/signing.cpp',*crypto,'-o',str(binary)],cwd=ROOT,check=True)
    subprocess.run([str(binary),str(keys[0][1]),*[str(temp/name) for name in fixtures]],check=True)
    packages=temp/'packages/esp32s3';packages.mkdir(parents=True);(packages/'native-demo.gapp').write_bytes(signed)
    site=temp/'site';first=catalog.build(packages.parent,site,'https://example.com',keys[0][1]);assert len(first['apps'])==1
    (packages/'native-demo.gapp').write_bytes(sign_package(unsigned,*keys[0]))
    assert catalog.build(packages.parent,site,'https://example.com',keys[0][1])==first
    (packages/'native-demo.gapp').write_bytes(fixtures['bad-untrusted'])
    try:catalog.build(packages.parent,site,'https://example.com',keys[0][1]);raise AssertionError('Untrusted catalog package accepted')
    except ValueError:pass
    print('PASS: catalog generation, immutable rebuild reuse, untrusted publication rejected')
