import {test} from 'node:test';
import assert from 'node:assert/strict';
import {CompanionService} from '../src/desktop-service.mjs';
import {snapshotRoute} from '../src/protocol.mjs';
const device={id:'AABBCCDDEEFF',name:'Desk Gadget',address:'192.168.1.42'};
const paired={...device,token:'a'.repeat(64)};
const summary={observed:Math.floor(Date.now()/1000),windows:[null,null]};
function fixture(options={}){
  let saved=options.saved??null;const calls=[];
  const service=new CompanionService({autoPoll:false,pause:async()=>{},
    storage:{load:async()=>saved,save:async(_path,value)=>{saved=structuredClone(value);},remove:async()=>{saved=null;}},
    transport:{discover:async()=>[device],request:async(address,path,data)=>{calls.push({address,path,data});return {status:path==='/v1/pair'?202:200,body:{system:1,systemRefresh:0}};}},
    collector:{sample:async()=>({schema:1,cpu:42})},collect:async()=>summary,...options});
  return {service,calls,saved:()=>saved};
}
test('graphical pairing saves only after device approval; public state contains no key',async()=>{
  const f=fixture();await f.service.initialize();await f.service.search();await f.service.pair(device.id);
  assert.equal(f.service.status().phase,'connected');assert.match(f.saved().token,/^[a-f0-9]{64}$/);
  assert(!JSON.stringify(f.service.status()).includes(f.saved().token));
  assert.equal(f.calls[0].path,'/v1/pair');assert.equal(f.calls[1].path,'/v1/status');
  await f.service.tick();assert(f.calls.some(c=>c.path==='/v1/system'));assert(!f.calls.some(c=>c.path==='/v1/agent-usage'));
  await f.service.forget();assert.equal(f.saved(),null);assert.equal(f.service.status().device,null);
});
test('system monitoring continues when optional Codex collection fails',async()=>{
  const f=fixture({saved:paired,collect:async()=>{throw new Error('Codex is missing.');}});await f.service.initialize();
  f.service.configureCodex(true);await f.service.checkCodex();await f.service.tick();
  assert.equal(f.service.status().phase,'connected');assert.equal(f.service.status().codex.state,'setup');
  assert(f.calls.some(c=>c.path==='/v1/system'));assert(!f.calls.some(c=>c.path==='/v1/agent-usage'));
});
test('lost devices rediscover only the saved ID and retain their pairing',async()=>{
  const f=fixture({saved:paired});await f.service.initialize();
  f.service.transport={request:async()=>{throw new Error('offline');},discover:async()=>[{...device,id:'001122334455',address:'192.168.1.99'},{...device,address:'192.168.1.43'}]};
  await f.service.tick();assert.equal(f.saved().address,'192.168.1.43');assert.equal(f.saved().token,paired.token);assert.equal(f.service.status().phase,'offline');
});
test('removed pairing requests repair without silently pairing or leaking keys',async()=>{
  const f=fixture({saved:paired});await f.service.initialize();f.service.transport.request=async()=>({status:403});
  await f.service.tick();assert.equal(f.service.status().phase,'repair');assert(!JSON.stringify(f.service.status()).includes(paired.token));
});
test('cancelling an in-flight pairing cannot overwrite the saved state',async()=>{
  let release,started;const gate=new Promise(r=>{release=r;}),waiting=new Promise(r=>{started=r;});
  const f=fixture({pause:async()=>{started();await gate;}});await f.service.search();const task=f.service.pair(device.id);await waiting;
  f.service.cancelPairing();release();await task;assert.equal(f.saved(),null);assert.equal(f.service.status().phase,'setup');
});
test('cancellation during persistence rolls back before forget or another pairing',async()=>{
  let release,started,saved=null;const gate=new Promise(r=>{release=r;}),waiting=new Promise(r=>{started=r;});
  const f=fixture({storage:{load:async()=>null,save:async(_path,value)=>{started();await gate;saved=value;},remove:async()=>{saved=null;}}});
  await f.service.search();const pairing=f.service.pair(device.id);await waiting;f.service.cancelPairing();release();await pairing;
  assert.equal(saved,null);
});
test('stopped collectors cannot publish a late Codex result',async()=>{
  let resolve;const f=fixture({collect:()=>new Promise(r=>{resolve=r;})});f.service.configureCodex(true);
  const collecting=f.service.checkCodex();f.service.stop();resolve(summary);await collecting;
  assert.equal(f.service.summary,null);
});
test('pairing timeouts and refused requests preserve the previous device',async()=>{
  let now=0;const f=fixture({saved:paired,clock:()=>now,pause:async()=>{now+=120001;}});await f.service.initialize();await f.service.search();
  f.service.transport.request=async()=>({status:202});await f.service.pair(device.id);
  assert.equal(f.service.status().phase,'choose');assert.match(f.service.status().message,/timed out/);assert.equal(f.saved().token,paired.token);
});
test('invalid/manual addresses and untrusted selection never reach the network',async()=>{
  const f=fixture();await assert.rejects(f.service.search('8.8.8.8'),/local IPv4/);await f.service.search();await assert.rejects(f.service.pair('UNKNOWN'),/discovered/);
  assert.equal(f.calls.length,0);
});
test('forget during a pending request prevents subsequent collection or delivery',async()=>{
  let complete,started;const pending=new Promise(r=>{complete=r;}),waiting=new Promise(r=>{started=r;});
  const f=fixture({saved:paired});await f.service.initialize();
  f.service.transport.request=async()=>{started();return pending;};
  const task=f.service.tick();await waiting;await f.service.forget();complete({status:200,body:{system:1}});await task;
  assert.equal(f.saved(),null);assert.equal(f.service.status().phase,'setup');assert.equal(f.service.summary,null);
});
test('activity is sent independently of Codex quotas and only to capable firmware',async()=>{
 const f=fixture({saved:paired,activityCollector:{sample:async()=>({schema:1,age:0,count:0,total:0,attention:0,enabled:1})}});await f.service.initialize();
 const paths=[];f.service.transport.request=async(_address,path)=>{paths.push(path);return {status:200,body:{activity:1}};};
 await f.service.tick();assert(paths.includes('/v1/activity'));assert(!paths.includes('/v1/agent-usage'));
 paths.length=0;f.service.transport.request=async(_address,path)=>{paths.push(path);return {status:200,body:{}};};await f.service.tick();assert(!paths.includes('/v1/activity'));
});
test('new firmware receives unchanged app payloads on generic authenticated routes',async()=>{
 const activity={schema:37,age:0,future:{metric:123},unknown:'preserved'};
 const system={schema:29,renamed_metric:42};
 const f=fixture({saved:paired,activityCollector:{sample:async()=>activity},collector:{sample:async()=>system}});
 await f.service.initialize();f.service.configureCodex(true);await f.service.checkCodex();
 const calls=[];f.service.transport.request=async(_address,path,options)=>{calls.push({path,options});return {status:200,body:{appChannels:1}};};
 await f.service.tick();
 assert.equal(f.service.status().phase,'connected');
 assert.deepEqual(calls.map(c=>c.path),['/v1/status','/v1/apps/system-monitor/system','/v1/apps/agent-usage/activity','/v1/apps/agent-usage/usage']);
 assert.deepEqual(calls[1].options.data,system);assert.deepEqual(calls[2].options.data,activity);
 assert(calls.every(c=>c.options.token===paired.token));
 assert.equal(snapshotRoute({},'agent-usage','activity','/v1/activity'),'/v1/activity');
 assert.equal(snapshotRoute({appChannels:1},'new-app','new-channel'),'/v1/apps/new-app/new-channel');
 assert.throws(()=>snapshotRoute({},'new-app','new-channel'),/Update Gadget OS/);
 for(const channel of ['../usage','a/b','a?x=1','', 'A'])assert.throws(()=>snapshotRoute({appChannels:1},'new-app',channel),/Invalid/);
});
