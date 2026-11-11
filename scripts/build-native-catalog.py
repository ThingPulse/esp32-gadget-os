#!/usr/bin/env python3
"""Merge verified packages into a static Pages site; preserve immutable versions."""
import argparse, hashlib, json, pathlib, re, shutil, struct, sys, tempfile, zlib
from urllib.parse import urlparse
ROOT=pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'experiments/native-app'))
from package_tools import inspect_signed

def png(icon):
    scan=bytearray()
    for y in range(40):
        scan.append(0)
        for x in range(40):
            n=struct.unpack_from('<H',icon,2*(y*40+x))[0]
            scan.extend(((n>>11)*255//31,((n>>5)&63)*255//63,(n&31)*255//31))
    def chunk(kind,body):return struct.pack('>I',len(body))+kind+body+struct.pack('>I',zlib.crc32(kind+body))
    return b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('>IIBBBBB',40,40,8,2,0,0,0))+chunk(b'IDAT',zlib.compress(scan))+chunk(b'IEND',b'')

def build(packages, site, base, public):
    url=urlparse(base)
    if url.scheme!='https' or not url.hostname or url.username or url.password or url.query or url.fragment:
        raise ValueError('Public base URL must be HTTPS without credentials, query or fragment')
    shutil.copytree(ROOT/'tools/gadget-companion/site',site/'companion',dirs_exist_ok=True)
    folder=site/'apps';folder.mkdir(parents=True,exist_ok=True)
    index=folder/'catalog.json'
    catalog=json.loads(index.read_text()) if index.exists() else {'schema':1,'apps':[]}
    if catalog.get('schema')!=1 or not isinstance(catalog.get('apps'),list):raise ValueError('Unsupported existing catalog')
    metadata=json.loads((ROOT/'experiments/native-app/apps.json').read_text())
    retired=set(metadata.get('retired_ids',[]))
    entries={(a['id'],a['chip']):a for a in catalog['apps'] if a['id'] not in retired}
    descriptions={a['id']:a.get('description','') for a in metadata['apps']}
    files=sorted(packages.glob('*/*.gapp'))
    if not files:raise ValueError('No packages found')
    with tempfile.TemporaryDirectory() as temporary:
        for source in files:
            data=source.read_bytes();m,icon=inspect_signed(data,public,temporary)
            if not re.fullmatch(r'[a-z][a-z0-9-]{0,31}',m['id']) or not re.fullmatch(r'[0-9]+\.[0-9]+\.[0-9]+(?:-[a-zA-Z0-9.-]+)?',m['version']) or m['chip'] not in ('esp32','esp32s3'):
                raise ValueError('Invalid catalog identity/version/chip')
            if m['api_major']!=1 or m['api_minor'] not in (0,1,2,3,4,5,6,7):raise ValueError('Unsupported host API')
            # Retain immutable historical files, but never advertise retired apps.
            if m['id'] in retired:continue
            relative=f"apps/packages/{m['id']}/{m['version']}/{m['chip']}.gapp"
            target=site/relative;target.parent.mkdir(parents=True,exist_ok=True)
            if target.exists():
                previous=target.read_bytes()
                # ECDSA nonces differ on rebuild; identical authenticated content can reuse the published file.
                if previous[:60]!=data[:60] or previous[124:]!=data[124:]:raise ValueError('Version already published with different contents: '+relative)
                inspect_signed(previous,public,temporary);data=previous
            else:target.write_bytes(data)
            image=relative.replace('/packages/','/icons/').replace('.gapp','.png')
            (site/image).parent.mkdir(parents=True,exist_ok=True);(site/image).write_bytes(png(icon))
            entries[(m['id'],m['chip'])]={**{k:m[k] for k in ('id','name','version','chip','api_major','api_minor')},'url':base.rstrip('/')+'/'+relative,'icon':base.rstrip('/')+'/'+image,'bytes':len(data),'sha256':hashlib.sha256(data).hexdigest()}
            description=descriptions.get(m['id'],'')
            if len(description.encode())>240 or any(ord(c)<32 for c in description):raise ValueError('Invalid app description')
            entries[(m['id'],m['chip'])]['description']=description
    catalog={'schema':1,'apps':sorted(entries.values(),key=lambda a:(a['id'],a['chip']))}
    encoded=json.dumps(catalog,indent=2)+'\n'
    if len(entries)>128 or len(encoded.encode())>65536:raise ValueError('Catalog capacity exceeded')
    index.write_text(encoded);(site/'.nojekyll').touch()
    return catalog

if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--packages',type=pathlib.Path,required=True)
    parser.add_argument('--site',type=pathlib.Path,required=True)
    parser.add_argument('--base-url',default='https://thingpulse.github.io/esp32-gadget-os')
    parser.add_argument('--public-key',type=pathlib.Path,default=ROOT/'experiments/native-app/signing-public.pem')
    args=parser.parse_args()
    print(f"Published catalog contains {len(build(args.packages,args.site,args.base_url,args.public_key)['apps'])} chip variants")
