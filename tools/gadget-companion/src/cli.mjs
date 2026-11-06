#!/usr/bin/env node
import {parseArgs} from 'node:util';
import {hostname} from 'node:os';
import {createInterface} from 'node:readline/promises';
import {setTimeout as delay} from 'node:timers/promises';
import {discover,request,newToken,pairingCode} from './protocol.mjs';
import {collectCodex,snapshot} from './codex.mjs';
import {SystemCollector} from './system.mjs';
import {defaultState,loadState,saveState} from './state.mjs';

const help=`Gadget Companion — Codex usage on your Gadget, over local Wi-Fi.

First: open Add apps > Pair computer on the Gadget. Then run this command.
Confirm the matching six-digit code on its screen. Subsequent runs reconnect.

Options:
  --pair              Pair again / choose another Gadget
  --device <IPv4>     Discover this LAN address if broadcast is blocked
  --codex <path>      Codex CLI executable (default: codex)
  --state <file>      Pairing file (default: ~/.gadget-companion/pairing.json)
  --snapshot <file>   Write simulator JSON instead of connecting to a Gadget
  --once              Collect and send once, then exit
  --system-snapshot <file>  Write System Monitor simulator data (macOS)
  --no-codex          Run system monitoring without Codex collection
  --help              Show this help

Requires Node 22+. Codex usage also requires a signed-in Codex CLI. Keep this terminal running.
No cloud relay. Pairing and summaries use HTTP on your trusted local network.
Provider credentials never leave this computer. Press Ctrl+C to stop.
`;
let stopped=false;const stop=new AbortController();
process.on('SIGINT',()=>{stopped=true;stop.abort();});process.on('SIGTERM',()=>{stopped=true;stop.abort();});
async function pause(ms){try{await delay(ms,undefined,{signal:stop.signal});}catch{}}
async function select(devices){
  if(!devices.length)throw new Error('No Gadget found. Check Wi-Fi and the new firmware, or use --device <device IP>.');
  if(devices.length===1)return devices[0];
  for(const [i,d]of devices.entries())console.log(`  ${i+1}. ${d.name} (${d.address})`);
  if(!process.stdin.isTTY)throw new Error('Multiple Gadgets found. Use --device <device IP>.');
  const input=createInterface({input:process.stdin,output:process.stdout});
  try{const n=Number(await input.question('Choose a Gadget: '));if(!Number.isInteger(n)||n<1||n>devices.length)throw new Error('Invalid selection.');return devices[n-1];}finally{input.close();}
}
async function pair(device,statePath){
  const token=newToken(), name=hostname().replace(/[^\x20-\x7e]/g,'').slice(0,48)||'Computer';
  console.log(`Pairing with ${device.name}. Check code ${pairingCode(token)} on the Gadget, then confirm there.`);
  const started=await request(device.address,'/v1/pair',{data:{token,name}});
  if(started.status!==202)throw new Error('Open Add apps > Pair computer on the Gadget, then run again.');
  const deadline=Date.now()+120000;
  while(!stopped&&Date.now()<deadline){
    await pause(1000);if(stopped)break;
    const result=await request(device.address,'/v1/status',{token});
    if(result.status===200){const state={id:device.id,address:device.address,name:device.name,token};await saveState(statePath,state);console.log('Paired. Open Agent Usage on the Gadget.');return state;}
    if(result.status!==202)break;
  }
  throw new Error('Pairing cancelled or timed out. Open Pair computer and try again.');
}
async function main(){
  const {values:o}=parseArgs({options:{help:{type:'boolean'},pair:{type:'boolean'},once:{type:'boolean'},device:{type:'string'},codex:{type:'string'},state:{type:'string'},snapshot:{type:'string'},'system-snapshot':{type:'string'},'no-codex':{type:'boolean'}}});
  if(o.help){console.log(help);return;}
  if(Number(process.versions.node.split('.')[0])<22)throw new Error('Install Node.js 22 or newer.');
  console.log('Gadget Companion — Codex usage and system monitoring. Ctrl+C to stop.');
  if(o['system-snapshot']){const collector=new SystemCollector();while(!stopped){const began=Date.now(),data=await collector.sample();if(!data)throw new Error('System metrics currently require macOS.');await saveState(o['system-snapshot'],{...data,generated_at:Math.floor(Date.now()/1000)});if(o.once)break;await pause(Math.max(0,2000-(Date.now()-began)));}return;}
  if(o.snapshot){
    while(!stopped){
      try{const summary=await collectCodex({executable:o.codex||'codex',signal:stop.signal});await saveState(o.snapshot,snapshot(summary));console.log('Simulator snapshot updated.');}
      catch(e){if(o.once)throw e;console.log(e.message+' Existing snapshot retained.');}
      if(o.once)break;await pause(60000);
    }
    return;
  }
  const path=o.state||defaultState();let state=o.pair?null:await loadState(path);
  if(state&&(!/^[A-F0-9]{12}$/.test(state.id)||!/^[a-f0-9]{64}$/.test(state.token)))throw new Error('Invalid pairing file. Run with --pair.');
  console.log('Looking for your Gadget…');
  const devices=await discover({target:o.device});
  if(!state)state=await pair(await select(devices),path);
  else{
    const match=devices.find(d=>d.id===state.id);
    if(match){state.address=match.address;await saveState(path,state);}
    else if(o.device)throw new Error('That address did not identify the paired Gadget. Use --pair to select another.');
  }
  let summary=null,failed=false,nextCodex=0,codexTask=null,codexReady=false,lastRefresh=-1,systemAnnounced=false;
  const collector=new SystemCollector();
  const collect=()=>{nextCodex=Date.now()+60000;codexTask=collectCodex({executable:o.codex||'codex',signal:stop.signal}).then(next=>{if(!next.details&&summary?.details)next.details=summary.details;summary=next;codexReady=true;}).catch(e=>{if(!stopped)console.log(e.message+' System monitoring continues.');}).finally(()=>{codexTask=null;});return codexTask;};
  while(!stopped){const began=Date.now();
    try{
      const check=await request(state.address,'/v1/status',{token:state.token});
      if(check.status===403)throw Object.assign(new Error('Pairing was removed. Run again with --pair.'),{fatal:true});
      if(check.status!==200)throw new Error('Device is not ready.');
      if(!o['no-codex']&&!codexTask&&Date.now()>=nextCodex){const task=collect();if(o.once)await task;}
      if(check.body.system===1){
        const force=lastRefresh!==check.body.systemRefresh;lastRefresh=check.body.systemRefresh;
        const data=await collector.sample(force);
        if(data){const result=await request(state.address,'/v1/system',{token:state.token,data});if(result.status!==200)throw new Error('System snapshot was refused.');if(!systemAnnounced){console.log('System monitoring active — updates every 2 seconds.');systemAnnounced=true;}}
      }
      if(stopped)break;
      if(codexReady&&summary){const result=await request(state.address,'/v1/agent-usage',{token:state.token,data:snapshot(summary)});if(result.status!==200)throw new Error('Codex snapshot was refused.');codexReady=false;console.log(`${new Date().toLocaleTimeString()}: Codex summary sent.`);}
      if(failed)console.log('Device reconnected.');failed=false;
    }catch(e){
      if(e.fatal||o.once){stop.abort();throw e;}
      if(!failed)console.log(`${e.message} Reconnecting automatically…`);failed=true;
      const found=await discover({target:o.device}).catch(()=>[]),match=found.find(d=>d.id===state.id);
      if(match){state.address=match.address;await saveState(path,state);}
    }
    if(o.once)break;await pause(Math.max(0,(failed?10000:2000)-(Date.now()-began)));
  }
  if(codexTask){stop.abort();await codexTask;}

}
main().catch(e=>{if(!stopped){console.error(e.message);process.exitCode=1;}});
