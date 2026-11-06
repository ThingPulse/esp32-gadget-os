#!/usr/bin/env node
// Local transport integration test: no Gadget, Codex login or real quotas used.
import assert from 'node:assert/strict';
import dgram from 'node:dgram';
import http from 'node:http';
import {networkInterfaces,tmpdir} from 'node:os';
import {mkdtemp,writeFile,readFile,rm} from 'node:fs/promises';
import {spawn} from 'node:child_process';
import {join,resolve} from 'node:path';
import {once} from 'node:events';
import {localAddress,pairingCode} from '../tools/gadget-companion/src/protocol.mjs';
const ip=Object.values(networkInterfaces()).flat().find(e=>e&&!e.internal&&e.family==='IPv4'&&localAddress(e.address))?.address;
if(!ip)throw new Error('This transport test needs a private LAN IPv4 interface.');
if(process.platform==='win32')throw new Error('Run this executable-fixture integration test on macOS/Linux; portable module tests cover Windows.');
const dir=await mkdtemp(join(tmpdir(),'gadget-network-'));
const state=join(dir,'pairing.json'),fake=join(dir,'codex-fixture');
await writeFile(fake,`#!/usr/bin/env node
const rl=require('node:readline').createInterface({input:process.stdin});
rl.on('line',line=>{const m=JSON.parse(line);if(m.id===1)console.log(JSON.stringify({id:1,result:{}}));
if(m.id===3)console.log(JSON.stringify({id:3,error:{code:-32601}}));
if(m.id===2)console.log(JSON.stringify({id:2,result:{rateLimits:{primary:{usedPercent:28,windowDurationMins:300,resetsAt:Math.floor(Date.now()/1000)+3600}}}}));});
`,{mode:0o700});
let token='',paired=false,pairings=0,pushes=0,revoked=false;
const udp=dgram.createSocket('udp4');
udp.on('message',(bytes,remote)=>{
  const match=bytes.toString().match(/^GADGET_COMPANION_DISCOVER_1 ([a-f0-9]{32})$/);if(!match)return;
  udp.send(JSON.stringify({protocol:1,nonce:match[1],id:'AABBCCDDEEFF',name:'Test Gadget',port:8082}),remote.port,remote.address);
});
const server=http.createServer((req,res)=>{
  let body='';req.on('data',chunk=>body+=chunk);req.on('end',()=>{
    res.setHeader('Content-Type','application/json');
    assert.equal(req.headers.origin,undefined);assert.equal(req.headers.cookie,undefined);
    if(req.url==='/v1/pair'){
      const d=JSON.parse(body);assert.match(d.token,/^[a-f0-9]{64}$/);token=d.token;pairings++;res.statusCode=202;res.end('{"pending":true}');return;
    }
    if(revoked||req.headers['x-companion-key']!==token){res.statusCode=403;res.end('{}');return;}
    if(req.url==='/v1/status'){paired=true;res.end('{"paired":true,"protocol":1}');return;}
    if(req.url==='/v1/agent-usage'){
      assert(paired);const d=JSON.parse(body);assert.equal(d.schema,1);assert.equal(d.codex_primary_used,28);assert.equal(d.claude_enabled,0);assert(d.codex_age<5);pushes++;res.end('{"ok":true}');return;
    }
    res.statusCode=404;res.end('{}');
  });
});
async function run(){
  const child=spawn(process.execPath,['tools/gadget-companion/src/cli.mjs','--device',ip,'--codex',fake,'--state',state,'--once'],{cwd:resolve('.'),stdio:['ignore','pipe','pipe']});
  let output='';child.stdout.on('data',b=>output+=b);child.stderr.on('data',b=>output+=b);
  const timeout=setTimeout(()=>child.kill('SIGKILL'),20000);
  const [code]=await once(child,'close');clearTimeout(timeout);return {code,output};
}
try{
  udp.bind(8083,ip);await once(udp,'listening');server.listen(8082,ip);await once(server,'listening');
  const first=await run();assert.equal(first.code,0,first.output);assert(first.output.includes(pairingCode(token)));assert(!first.output.includes(token));
  assert.equal(JSON.parse(await readFile(state,'utf8')).token,token);
  const second=await run();assert.equal(second.code,0,second.output);assert.equal(pairings,1);assert.equal(pushes,2);assert(!second.output.includes(token));
  revoked=true;const third=await run();assert.equal(third.code,1);assert(third.output.includes('--pair'));assert.equal(pushes,2);
  console.log('PASS: UDP discovery, CLI pairing/code, HTTP push, saved-pair reconnect and revoked-token handling (local device emulator).');
}finally{udp.close();server.closeAllConnections();await new Promise(r=>server.close(r));await rm(dir,{recursive:true,force:true});}
