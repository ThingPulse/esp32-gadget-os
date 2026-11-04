#!/usr/bin/env python3
"""Exercise a paired development device. Creates/removes only installer-probe.
The private pairing URL is read from a file, never printed. Optional command-file
is an append-only queue consumed by the serial monitor (one command per second).
"""
import argparse,hashlib,json,pathlib,socket,struct,time,urllib.request,urllib.parse,urllib.error
p=argparse.ArgumentParser();p.add_argument('--pairing-file',type=pathlib.Path,required=True);p.add_argument('--package',type=pathlib.Path,required=True);p.add_argument('--command-file',type=pathlib.Path);a=p.parse_args()
u=urllib.parse.urlsplit(a.pairing_file.read_text().strip());base=u.scheme+'://'+u.netloc
assert u.scheme=='http' and u.fragment
raw=a.package.read_bytes();ml,il,el=struct.unpack_from('<III',raw,8);meta=json.loads(raw[52:52+ml]);icon=raw[52+ml:52+ml+il];elf=raw[-el:]
probe='installer-probe'
def package(version='0.1.0',**changes):
 m=dict(meta,id=probe,name='Installer Probe',version=version);m.update(changes)
 b=json.dumps(m).encode();b+=b' '*(-len(b)%4);payload=b+icon+elf
 h=b'GOSAPP1\0'+struct.pack('<III',len(b),len(icon),len(elf));return h+hashlib.sha256(h+payload).digest()+payload

def request(route='/api/apps',method='GET',body=None,status=200,headers=None):
 h={'X-App-Key':u.fragment,'Content-Type':'application/octet-stream'};h.update(headers or {})
 try:r=urllib.request.urlopen(urllib.request.Request(base+route,data=body,headers=h,method=method),timeout=12)
 except urllib.error.HTTPError as e:r=e
 data=r.read();assert r.status==status,(route,r.status,status,data)
 return json.loads(data)
def install(data,status=200):return request('/api/install','POST',data,status)
def state():return next((x for x in request()['apps'] if x['id']==probe),None)
def command(line):
 with a.command_file.open('a') as f:f.write(line+'\n')
def wait_running(value):
 end=time.monotonic()+10
 while time.monotonic()<end:
  s=state()
  if s and s['running']==value:return
  time.sleep(.25)
 raise AssertionError('App running state did not change')
assert state() is None,'Refusing to replace an existing installer-probe app'
request(status=403,headers={'X-App-Key':'wrong'})
request(status=403,headers={'Origin':'http://untrusted.invalid'})
request(status=403,headers={'Host':'untrusted.invalid:8081'})
print('PASS: pairing, Origin and Host checks',flush=True)
install(package(chip='wrong-chip'),422);install(package(api_major=2),422);install(package(id='../escape'),422)
install(package());assert state()['version']=='0.1.0'
install(package('0.2.0'));assert state()['version']=='0.2.0'
bad=bytearray(package('0.3.0'));bad[-1]^=1;install(bad,422)
install(package('0.3.0')[:-1],422);assert state()['version']=='0.2.0'
print('PASS: generic ID install, atomic replacement, incompatible/corrupt/truncated package rejection',flush=True)
body=package('0.3.0')
with socket.create_connection((u.hostname,u.port),timeout=5) as s:
 h=f'POST /api/install HTTP/1.1\r\nHost: {u.netloc}\r\nX-App-Key: {u.fragment}\r\nContent-Length: {len(body)}\r\n\r\n'
 s.sendall(h.encode()+body[:300]);time.sleep(.2)
time.sleep(.4);assert state()['version']=='0.2.0'
# An incomplete header must not contaminate the next peer either.
with socket.create_connection((u.hostname,u.port),timeout=5) as s:s.sendall(b'POST /api/inst')
time.sleep(.4);assert state()['version']=='0.2.0'
print('PASS: interrupted body/header retain prior app; following requests recover',flush=True)
if a.command_file:
 command('native manager close');command('native run '+probe);wait_running(True)
 install(package('0.3.0'),409);request('/api/apps/'+probe,'DELETE',status=409)
 command('native close');wait_running(False)
 print('PASS: dynamically installed executable runs; running update/remove rejected; closure works',flush=True)
# Fill the published app limit, reject one more, and still permit an update.
initial=request()['apps'];reserved=['installer-slot-'+chr(97+i) for i in range(8-len(initial))]
assert not any(x['id'] in reserved+['installer-overflow'] for x in initial)
try:
 for app_id in reserved:install(package(id=app_id))
 assert len(request()['apps'])==8
 install(package(id='installer-overflow'),409)
 install(package('0.3.0'));assert state()['version']=='0.3.0'
 print('PASS: eight-app limit and replacement while full',flush=True)
finally:
 for app_id in reserved:
  if any(x['id']==app_id for x in request()['apps']):request('/api/apps/'+app_id,'DELETE')
request('/api/apps/'+probe,'DELETE');assert state() is None
print('PASS: removal and catalog refresh',flush=True)
