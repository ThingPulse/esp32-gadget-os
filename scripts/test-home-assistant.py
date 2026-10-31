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
from types import SimpleNamespace
from jinja2.sandbox import ImmutableSandboxedEnvironment
root = Path(__file__).resolve().parent.parent
os.chdir(root)
subprocess.run(['node','test/home-assistant/setup-browser.test.cjs'],check=True)
commands = []
large_mode = False
template_forbidden = False
ha_requests = []
template_sizes = []
templates = ImmutableSandboxedEnvironment()
templates.filters["to_json"] = json.dumps
class States(list):
 def __getitem__(self, key):
  if isinstance(key, str): return [s for s in self if s.entity_id.startswith(key+".")]
  return super().__getitem__(key)
entities = [
 {'entity_id':'light.sofa','state':'on','attributes':{'friendly_name':'Sofa light','brightness':166,'color_temp_kelvin':2700,'min_color_temp_kelvin':2000,'max_color_temp_kelvin':6500,'supported_color_modes':['color_temp','rgb']}},
 {'entity_id':'switch.ceiling','state':'off','attributes':{'friendly_name':'Ceiling'}},
 {'entity_id':'sensor.temperature','state':'22.5','attributes':{'friendly_name':'Temperature','unit_of_measurement':'°C'}},
 {'entity_id':'sensor.humidity','state':'48','attributes':{'friendly_name':'Humidity','unit_of_measurement':'%'}},
 {'entity_id':'scene.movie','state':'2026-10-28T10:00:00Z','attributes':{'friendly_name':'Movie night'}},
 {'entity_id':'script.all_off','state':'off','attributes':{'friendly_name':'All off'}},
 {'entity_id':'sensor.solar','state':'2.4','attributes':{'friendly_name':'Solar','unit_of_measurement':'kW'}}]
large_entities = entities + [{'entity_id':f'sensor.extra_{i}','state':'1','attributes':{'friendly_name':f'Extra {i}','ignored_payload':'x' * (300000 if i==0 else 1800)}} for i in range(1500)]
assert len(json.dumps(large_entities)) > 1048576
class Handler(http.server.BaseHTTPRequestHandler):
 def log_message(self,*args): pass
 def do_GET(self): self.handle_request()
 def do_POST(self): self.handle_request()
 def handle_request(self):
  ha_requests.append(self.path)
  code=200;data=large_entities if large_mode or self.path.endswith(("?large","?chunked")) else entities
  if self.headers.get('Authorization')!='Bearer test-token': code=401;data={}
  elif self.command=='POST':
   body=json.loads(self.rfile.read(int(self.headers.get('Content-Length',0))))
   if self.path=='/api/template':
    assert body['variables']['limit']==25
    if template_forbidden: code=403;data={}
    else:
     states=States(SimpleNamespace(entity_id=e['entity_id'],name=e['attributes'].get('friendly_name',e['entity_id']),state=e['state']) for e in data)
     rendered=templates.from_string(body['template']).render(states=states,**body['variables'])
     template_sizes.append(len(rendered));data=json.loads(rendered)
     assert len(data['entities'])<=25 and 'ignored_payload' not in rendered
   else: commands.append((self.path,body));data=[]
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
 env=dict(os.environ,ASAN_OPTIONS='detect_leaks=0',GADGET_OS_DATA_DIR=temporary,GADGET_OS_SETUP_EPHEMERAL_PORT='1')
 subprocess.run([str(binary),url],env=env,check=True,timeout=45)
 assert ('/api/services/light/turn_on',{'entity_id':'light.sofa','brightness_pct':65}) in commands
 assert ('/api/services/light/turn_on',{'entity_id':'light.sofa','rgb_color':[255,0,128]}) in commands
 # Exercise the actual bounded HTTP listener and pairing authentication.
 proc=subprocess.Popen([str(binary),url,'--serve'],env=env,stdout=subprocess.PIPE,text=True)
 try:
  line=proc.stdout.readline().strip()
  while not line.startswith('http://127.0.0.1:'):
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
  before=len(ha_requests)
  state=discover({'kind':'light'});assert state['total']==1 and len(state['entities'])==1
  state=discover({});assert state['total']==1507 and len(state['entities'])==25 and state['page_size']==25
  state=discover({'offset':1500});assert state['offset']==1500 and len(state['entities'])==7
  state=discover({'query':'extra_1499','kind':'sensor'});assert state['total']==1 and state['entities'][0]['id']=='sensor.extra_1499'
  # Filtering happens before counting and pagination, including unknown states.
  large_entities[7]['state']='unavailable';large_entities[8]['state']='unknown'
  state=discover({});assert state['hide_unavailable'] and state['total']==1505 and all(e['available'] for e in state['entities'])
  state=discover({'offset':1500});assert len(state['entities'])==5
  state=discover({'query':'extra_0'});assert state['total']==0
  state=discover({'hide_unavailable':False,'query':'extra_0'});assert not state['hide_unavailable'] and state['total']==1 and not state['entities'][0]['available']
  state=discover({'hide_unavailable':False});assert state['total']==1507
  # User input is a variable, never executable template text.
  state=discover({'query':"' }}{{ 7*7 }}{%",'kind':'sensor'});assert state['total']==0
  assert set(ha_requests[before:])=={'/api/template'} and max(template_sizes)<10000
  template_forbidden=True
  json.load(urllib.request.urlopen(urllib.request.Request(origin+'/api/discover',data=b'{"kind":"light"}',headers=headers)))
  import time
  for _ in range(100):
   state=json.load(urllib.request.urlopen(urllib.request.Request(origin+'/api/status',headers=headers)))
   if not state['busy']: break
   time.sleep(.05)
  assert 'administrator token' in state['error'] and set(ha_requests[before:])=={'/api/template'}
  template_forbidden=False
  print('HTTP pairing, persistence, template filtering of 1507 entities, bounded payloads and permission recovery passed', flush=True)
  if os.environ.get('HOME_SETUP_URL_PATH'):
   import time
   Path(os.environ['HOME_SETUP_URL_PATH']).write_text(line)
   time.sleep(240)
 finally: proc.terminate();proc.wait(timeout=5)
server.shutdown()
