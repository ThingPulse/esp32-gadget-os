#!/usr/bin/env python3
"""Build native first. Uses a local fake HA; never contacts a real installation."""
import http.server
import gzip
import json
import os
from pathlib import Path
import shlex
import subprocess
import tempfile
import threading
import urllib.request
import urllib.error
root = Path(__file__).resolve().parent.parent
os.chdir(root)
subprocess.run(['node','test/home-assistant/setup-browser.test.cjs'],check=True)
commands = []
large_mode = False
entities = [
 {'entity_id':'light.sofa','state':'on','attributes':{'friendly_name':'Sofa light','brightness':166,'color_temp_kelvin':2700,'min_color_temp_kelvin':2000,'max_color_temp_kelvin':6500,'supported_color_modes':['color_temp']}},
 {'entity_id':'switch.ceiling','state':'off','attributes':{'friendly_name':'Ceiling'}},
 {'entity_id':'sensor.temperature','state':'22.5','attributes':{'friendly_name':'Temperature','unit_of_measurement':'°C'}},
 {'entity_id':'sensor.humidity','state':'48','attributes':{'friendly_name':'Humidity','unit_of_measurement':'%'}},
 {'entity_id':'scene.movie','state':'2026-10-28T10:00:00Z','attributes':{'friendly_name':'Movie night'}},
 {'entity_id':'script.all_off','state':'off','attributes':{'friendly_name':'All off'}},
 {'entity_id':'sensor.solar','state':'2.4','attributes':{'friendly_name':'Solar','unit_of_measurement':'kW'}}]
large_entities = entities + [{'entity_id':f'sensor.extra_{i}','state':'1','attributes':{'friendly_name':f'Extra {i}','ignored_payload':'x' * (300000 if i==0 else 1800)}} for i in range(600)]
assert len(json.dumps(large_entities)) > 1048576
class Handler(http.server.BaseHTTPRequestHandler):
 def log_message(self,*args): pass
 def do_GET(self): self.handle_request()
 def do_POST(self): self.handle_request()
 def handle_request(self):
  code=200;data=large_entities if large_mode or self.path.endswith(("?large","?chunked")) else entities
  if self.headers.get('Authorization')!='Bearer test-token': code=401;data={}
  elif self.command=='POST':
   body=json.loads(self.rfile.read(int(self.headers.get('Content-Length',0))));commands.append((self.path,body));data=[]
  elif self.path.startswith('/api/states/'):
   data=next((e for e in entities if e['entity_id']==self.path.split('/')[-1]),None)
   if data is None: code=404;data={}
  body=json.dumps(data).encode();self.send_response(code);self.send_header('Content-Type','application/json')
  if self.path.endswith('?chunked'):
   self.send_header('Transfer-Encoding','chunked');self.end_headers()
   for i in range(0,len(body),8191):
    chunk=body[i:i+8191];self.wfile.write(f'{len(chunk):x}\r\n'.encode()+chunk+b'\r\n')
   self.wfile.write(b'0\r\n\r\n')
  else:
   self.send_header('Content-Length',len(body));self.end_headers();self.wfile.write(body)
server=http.server.ThreadingHTTPServer(('127.0.0.1',0),Handler)
threading.Thread(target=server.serve_forever,daemon=True).start()
url=f'http://127.0.0.1:{server.server_port}'
build=root/'.pio/build/native'
objects=[p for p in (build/'src').rglob('*.o') if p.name not in {'HomeAssistantApp.o','HomeAssistantApp.cpp.o','sdl_main.o','sdl_main.cpp.o'}]
for library in build.glob('lib*'):
 archives=list(library.glob('*.a'));objects.extend(archives or list(library.rglob('*.o')))
with tempfile.TemporaryDirectory(prefix='home-test-') as temporary:
 binary=Path(temporary)/'regression'
 flags=shlex.split(subprocess.check_output(['sdl2-config','--libs'],text=True))
 subprocess.run(['c++','-std=c++14','-fsanitize=address','-pthread','-DLV_CONF_SKIP','-DLV_USE_QRCODE=1','-DLV_FONT_MONTSERRAT_24=1','-Iinclude','-I.pio/libdeps/native/lvgl','test/home-assistant/regression.cpp',*map(str,objects),*flags,'-o',str(binary)],check=True)
 env=dict(os.environ,ASAN_OPTIONS='detect_leaks=0',GADGET_OS_DATA_DIR=temporary)
 subprocess.run([str(binary),url],env=env,check=True,timeout=45)
 assert ('/api/services/light/turn_on',{'entity_id':'light.sofa','brightness_pct':65}) in commands
 # Exercise the actual bounded HTTP listener and pairing authentication.
 proc=subprocess.Popen([str(binary),url,'--serve'],env=env,stdout=subprocess.PIPE,text=True)
 try:
  line=proc.stdout.readline().strip()
  while not line.startswith('http://127.0.0.1:8080/'):
   if proc.poll() is not None: raise RuntimeError('Setup server failed')
   line=proc.stdout.readline().strip()
  origin,key=line.split('/#');headers={'X-Setup-Key':key,'Content-Type':'application/json'}
  page=gzip.decompress(urllib.request.urlopen(origin+'/').read()).decode();assert 'Save to device' in page
  try: urllib.request.urlopen(origin+'/api/config');raise AssertionError('Unauthenticated request allowed')
  except urllib.error.HTTPError as e: assert e.code==403
  public=json.load(urllib.request.urlopen(urllib.request.Request(origin+'/api/config',headers=headers)))
  assert 'token' not in public and public['has_token']
  public['pages'][0]['name']='Bedroom'
  result=json.load(urllib.request.urlopen(urllib.request.Request(origin+'/api/config',data=json.dumps(public).encode(),headers=headers)))
  assert result['ok']
  result=json.load(urllib.request.urlopen(urllib.request.Request(origin+'/api/config',headers=headers)));assert result['pages'][0]['name']=='Bedroom'
  large_mode=True
  def discover(options):
   import time
   json.load(urllib.request.urlopen(urllib.request.Request(origin+'/api/discover',data=json.dumps(options).encode(),headers=headers)))
   for _ in range(100):
    state=json.load(urllib.request.urlopen(urllib.request.Request(origin+'/api/status',headers=headers)))
    if not state['busy']:
     assert not state['error'],state['error']
     return state
    time.sleep(.05)
   raise AssertionError('Discovery timed out')
  state=discover({});assert state['total']==607 and len(state['entities'])==256
  state=discover({'offset':512});assert state['offset']==512 and len(state['entities'])==95
  state=discover({'query':'extra_599','kind':'sensor'});assert state['total']==1 and state['entities'][0]['id']=='sensor.extra_599'
  print('HTTP pairing, persistence and streamed discovery >1 MiB with pagination/search passed', flush=True)
  if os.environ.get('HOME_SETUP_URL_PATH'):
   import time
   Path(os.environ['HOME_SETUP_URL_PATH']).write_text(line)
   time.sleep(240)
 finally: proc.terminate();proc.wait(timeout=5)
server.shutdown()
