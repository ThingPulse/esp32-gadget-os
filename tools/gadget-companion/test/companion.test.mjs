import {test} from 'node:test';
import assert from 'node:assert/strict';
import {PassThrough} from 'node:stream';
import {EventEmitter} from 'node:events';
import {mkdtemp,rm,stat} from 'node:fs/promises';
import {tmpdir} from 'node:os';
import {join} from 'node:path';
import {normalize,snapshot,collectCodex,codexCommand,normalizeUsage,compactCount} from '../src/codex.mjs';
import {pairingCode,newToken,localAddress,broadcastAddresses,request} from '../src/protocol.mjs';
import {saveState,loadState} from '../src/state.mjs';
const bucket={primary:{usedPercent:28,windowDurationMins:300,resetsAt:1060},secondary:null};
test('Codex quotas retain their observation time; expiry and other buckets are not fabricated',()=>{
  const s=normalize({rateLimits:bucket},1000);
  assert.equal(snapshot(s,1010).codex_primary_used,28);assert.equal(snapshot(s,1010).codex_age,10);
  assert.equal(snapshot(s,1010).codex_primary_reset,50);assert.equal(snapshot(s,1060).codex_primary_used,-1);
  assert.equal(snapshot(null).codex_age,-1);assert.equal(snapshot(s,1010).claude_enabled,0);
  assert.throws(()=>normalize({rateLimitsByLimitId:{other:bucket},rateLimits:bucket},1000));
  assert.equal(normalize({rateLimits:{primary:{...bucket.primary,usedPercent:Infinity}}}).windows[0],null);
});
test('discovery restricted to LAN; broadcasts cover each private interface',async()=>{
  assert(localAddress('192.168.1.4'));assert(localAddress('172.16.0.1'));assert(!localAddress('172.32.1.2'));
  assert(!localAddress('127.0.0.1'));assert(!localAddress('example.com'));assert(!localAddress('8.8.8.8'));
  assert.deepEqual(broadcastAddresses({en0:[{internal:false,family:'IPv4',address:'192.168.2.42',netmask:'255.255.255.0'}]}),['255.255.255.255','192.168.2.255']);
  await assert.rejects(request('8.8.8.8','/v1/status'),/non-LAN/);
  await assert.rejects(request('192.168.1.1','/v1/agent-usage',{data:'x'.repeat(2049)}),/exceeds/);
});
test('pairing has stable matching code and private persistent state',async()=>{
  assert.equal(pairingCode('a'.repeat(64)),'891902');
  assert.match(newToken(),/^[a-f0-9]{64}$/);
  const dir=await mkdtemp(join(tmpdir(),'gadget-companion-'));const path=join(dir,'state.json');
  try{assert.equal(await loadState(path),null);const state={token:newToken(),id:'AABBCCDDEEFF'};await saveState(path,state);assert.deepEqual(await loadState(path),state);if(process.platform!=='win32')assert.equal((await stat(path)).mode&0o777,0o600);}
  finally{await rm(dir,{recursive:true,force:true});}
});
function fakeCodex({fail=false,hang=false,usageError=false}={}){
  const child=new EventEmitter();child.stdout=new PassThrough();child.stdin=new PassThrough();child.kill=()=>{queueMicrotask(()=>child.emit('close'));return true;};
  const calls=[];child.stdin.on('data',chunk=>{const m=JSON.parse(chunk);calls.push(m);
    if(hang)return;
    if(m.method==='initialize')queueMicrotask(()=>child.stdout.write(JSON.stringify({id:1,result:{}})+'\n'));
    if(m.method==='account/usage/read')queueMicrotask(()=>child.stdout.write(JSON.stringify(usageError?{id:3,error:{code:-32601}}:{id:3,result:{summary:{lifetimeTokens:1234567},dailyUsageBuckets:[]}})+'\n'));
    if(m.method==='account/rateLimits/read')queueMicrotask(()=>{
      child.stdout.write(JSON.stringify({id:99,method:'dangerous/request'})+'\n');
      child.stdout.write(JSON.stringify(fail?{id:2,error:{message:'private error'}}:{id:2,result:{rateLimits:bucket}})+'\n');
    });
  });return {child,calls};
}
test('read-only Codex RPC handshake, unsolicited requests rejected, process cleaned up',async()=>{
  const {child,calls}=fakeCodex();const result=await collectCodex({spawnProcess:(exe,args)=>{assert.equal(exe,'codex');assert.deepEqual(args,['app-server']);return child;}});
  assert.equal(result.windows[0].used,28);
  assert.deepEqual(calls.filter(c=>c.method).map(c=>c.method),['initialize','initialized','account/rateLimits/read','account/usage/read']);
  assert.equal(calls.find(c=>c.id===99).error.code,-32601);
});
test('Codex errors and timeout are bounded and redact server details',async()=>{
  await assert.rejects(collectCodex({spawnProcess:()=>fakeCodex({fail:true}).child}),/Check the CLI login/);
  await assert.rejects(collectCodex({spawnProcess:()=>fakeCodex({hang:true}).child,timeout:10}),/timed out/);
});

test('Windows npm wrapper launches Node without a command shell',()=>{
  const command=codexCommand('codex','win32',()=>'/npm/codex.cmd',()=>true);
  assert.equal(command.file,process.execPath);assert.equal(command.args[1],'app-server');assert(command.args[0].endsWith('codex.js'));
  assert.throws(()=>codexCommand('codex','win32',()=>'/npm/codex.cmd',()=>false),/native codex.exe/);
});

test('Ctrl+C cancels a stalled collector without waiting for its timeout',async()=>{
  const controller=new AbortController();const pending=collectCodex({spawnProcess:()=>fakeCodex({hang:true}).child,signal:controller.signal});
  controller.abort();await assert.rejects(pending,/Stopped/);
});

test('detail snapshots compact large counts, preserve missing days and remain bounded',()=>{
  const now=Date.parse('2026-11-06T10:00:00Z')/1000;
  const summary=normalize({rateLimits:bucket},now);
  summary.details=normalizeUsage({summary:{lifetimeTokens:12345678901,peakDailyTokens:0,currentStreakDays:8,longestStreakDays:null,longestRunningTurnSec:540},dailyUsageBuckets:[{startDate:'2026-11-06',tokens:12345},{startDate:'2026-11-05',tokens:0},{startDate:'2026-10-01',tokens:900},{startDate:'2026-11-99',tokens:999}]},now);
  const data=snapshot(summary,now+10);
  assert.deepEqual(compactCount(12345678901),{v:123,u:3});assert.deepEqual(compactCount(999999),{v:10,u:2});
  assert.equal(data.codex_detail_age,10);assert.equal(data.codex_lifetime_u,3);
  assert.equal(data.codex_d6_v,123);assert.equal(data.codex_d6_date,1106);assert.equal(data.codex_d6_bar,100);
  assert.equal(data.codex_d5_v,0);assert.equal(data.codex_d5_bar,0);assert.equal(data.codex_d4_v,undefined);
  assert.equal(data.codex_best_streak,undefined);assert(Buffer.byteLength(JSON.stringify(data))<2048);
  assert.equal(compactCount(Number.MAX_SAFE_INTEGER),null);assert.equal(compactCount(-1),null);assert.equal(compactCount(Number.MAX_SAFE_INTEGER+1),null);
});

test('older CLI lacking usage endpoint still supplies allowance',async()=>{
  const result=await collectCodex({spawnProcess:()=>fakeCodex({usageError:true}).child});
  assert.equal(result.windows[0].used,28);assert.equal(result.details,null);
});

test('UTC calendar periods preserve coverage, zeros and month boundaries',()=>{
 const now=Date.parse('2026-03-03T12:00:00Z')/1000;
 const s=normalize({rateLimits:bucket},now);s.details=normalizeUsage({summary:{},dailyUsageBuckets:[{startDate:'2026-02-28',tokens:900},{startDate:'2026-03-01',tokens:100},{startDate:'2026-03-02',tokens:0}]},now);
 const d=snapshot(s,now);
 assert.equal(d.cx_p0_n,0);assert.equal(d.cx_p0_v,undefined);assert.equal(d.cx_p0_e,1);
 assert.equal(d.cx_p1_n,3);assert.equal(d.cx_p1_e,7);assert.equal(d.cx_p1_v,10);assert.equal(d.cx_p1_u,1);
 assert.equal(d.cx_p2_n,2);assert.equal(d.cx_p2_e,3);assert.equal(d.cx_p2_v,100);assert.equal(d.cx_m0_v,100);assert.equal(d.cx_m1_v,undefined);
 const fullNow=Date.parse('2026-03-31T23:59:59Z')/1000;
 s.observed=fullNow;s.details=normalizeUsage({summary:{lifetimeTokens:1e10,peakDailyTokens:1e9,currentStreakDays:365,longestStreakDays:365,longestRunningTurnSec:999},dailyUsageBuckets:Array.from({length:31},(_,i)=>({startDate:`2026-03-${String(i+1).padStart(2,'0')}`,tokens:1000000+i}))},fullNow);
 const full=snapshot(s,fullNow);assert.equal(full.cx_p2_n,31);assert.equal(full.cx_p2_e,31);assert.equal(full.cx_m6_v,10);assert(Buffer.byteLength(JSON.stringify(full))<2048);
 const next=snapshot(s,fullNow+1);assert.equal(next.cx_p2_e,1);assert.equal(next.cx_p2_n,0);assert.equal(next.cx_p2_v,undefined);
});
