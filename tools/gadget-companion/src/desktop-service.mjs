import {EventEmitter} from 'node:events';
import {hostname} from 'node:os';
import {rm} from 'node:fs/promises';
import {snapshotRoute,discover,request,newToken,pairingCode,localAddress} from './protocol.mjs';
import {collectCodex,snapshot} from './codex.mjs';
import {ActivityCollector} from './activity.mjs';
import {SystemCollector} from './system.mjs';
import {defaultState,loadState,saveState} from './state.mjs';

// Transport and collectors are injected for deterministic lifecycle tests.
export class CompanionService extends EventEmitter {
  constructor({statePath=defaultState(),transport={discover,request},collect=collectCodex,
    collector=new SystemCollector(),activityCollector=new ActivityCollector(),clock=Date.now,pause=ms=>new Promise(r=>setTimeout(r,ms)),
    storage={load:loadState,save:saveState,remove:path=>rm(path,{force:true})},autoPoll=true}={}) {
    super();Object.assign(this,{statePath,transport,collect,collector,activityCollector,clock,pause,storage,autoPoll});
    this.persistence=Promise.resolve();this.generation=0;this.state=null;this.timer=null;this.abort=new AbortController();
    this.codexEnabled=false;this.codexExecutable='codex';this.summary=null;this.nextCodex=0;
    this.codexTask=null;this.lastRefresh=-1;this.devices=[];
    this.view={phase:'setup',message:'Connect your Gadget to this computer.',device:null,devices:[],code:null,
      codex:{state:'off',message:'Optional. Enable Agent Dashboard to check your Codex setup.'},system:{state:'waiting',message:'Works without Codex.'}};
  }
  persist(operation){const task=this.persistence.then(operation);this.persistence=task.catch(()=>{});return task;}
  status(){return structuredClone(this.view);}
  update(patch){Object.assign(this.view,patch);this.emit('change',this.status());}
  async initialize(){
    try {
      const state=await this.storage.load(this.statePath);
      if(state){
        if(!/^[A-F0-9]{12}$/.test(state.id)||!/^[a-f0-9]{64}$/.test(state.token)||!localAddress(state.address))throw new Error('Saved pairing is invalid. Forget this Gadget and pair again.');
        this.state=state;this.update({phase:'offline',device:this.publicDevice(state),message:'Reconnecting to your Gadget…'});this.schedule(0);
      }
    }catch(e){this.update({phase:'setup',message:e.message});}
    return this.status();
  }
  publicDevice(d){return {id:d.id,name:d.name,address:d.address};}
  cancelWork(){this.generation++;clearTimeout(this.timer);this.timer=null;this.abort.abort();this.abort=new AbortController();this.codexTask=null;}
  schedule(ms=2000){if(this.autoPoll&&this.state&&!this.timer)this.timer=setTimeout(()=>{this.timer=null;void this.tick();},ms);}
  async search(target){
    if(target&&!localAddress(target))throw new Error('Enter the Gadget’s local IPv4 address, such as 192.168.1.42.');
    this.cancelWork();const generation=this.generation;
    this.update({phase:'searching',message:'Looking for Gadgets on your local network…',code:null,devices:[]});
    try {
      const devices=await this.transport.discover({target,signal:this.abort.signal});if(generation!==this.generation)return;
      this.devices=devices;this.update({phase:'choose',devices:devices.map(d=>this.publicDevice(d)),message:devices.length?'Choose your Gadget. Open Add apps → Pair computer on its screen.':'No Gadget found. Check that both devices use the same Wi-Fi and allow local network access.'});
    }catch(e){if(generation===this.generation)this.update({phase:'choose',message:e.message});}
  }
  async pair(id){
    if(this.view.phase!=='choose')throw new Error('Search for your Gadget first.');
    const device=this.devices.find(d=>d.id===id);if(!device)throw new Error('Choose a discovered Gadget.');
    this.cancelWork();const generation=this.generation,signal=this.abort.signal,token=newToken();
    this.update({phase:'pairing',code:pairingCode(token),message:'Compare this code with your Gadget, then tap Confirm pairing on the Gadget.'});
    try {
      const response=await this.transport.request(device.address,'/v1/pair',{data:{token,name:hostname().replace(/[^\x20-\x7e]/g,'').slice(0,48)||'Computer'},signal});
      if(generation!==this.generation)return;
      if(response.status!==202)throw new Error('Open Add apps → Pair computer on your Gadget, then try again.');
      const deadline=this.clock()+120000;
      while(generation===this.generation&&this.clock()<deadline){
        await this.pause(1000);if(generation!==this.generation)return;
        const result=await this.transport.request(device.address,'/v1/status',{token,signal});
        if(generation!==this.generation)return;
        if(result.status===200){
          const next={...device,token};
          await this.persist(async()=>{
            if(generation!==this.generation)return;
            await this.storage.save(this.statePath,next);
            // A cancellation while the atomic write was in progress must not
            // silently replace the previous pairing on the next launch.
            if(generation!==this.generation){
              if(this.state)await this.storage.save(this.statePath,this.state);
              else await this.storage.remove(this.statePath);
            }
          });
          if(generation!==this.generation)return;
          this.state=next;this.summary=null;this.nextCodex=0;
          this.update({phase:'connected',device:this.publicDevice(next),code:null,message:'Paired. Open System Monitor or Agent Dashboard on your Gadget.'});this.schedule(0);return;
        }
        if(result.status!==202)break;
      }
      throw new Error('Pairing timed out. Open Pair computer on your Gadget and try again.');
    }catch(e){if(generation===this.generation)this.update({phase:'choose',code:null,message:e.message});}
  }
  cancelPairing(){this.cancelWork();this.update({phase:this.state?'offline':'setup',code:null,message:this.state?'Reconnecting to your Gadget…':'Pairing cancelled. You can try again.'});this.schedule(0);}
  async forget(){
    this.cancelWork();await this.persist(()=>this.storage.remove(this.statePath));this.state=null;this.summary=null;
    this.update({phase:'setup',device:null,devices:[],code:null,message:'Gadget forgotten on this computer. You can also choose Forget computer on the Gadget.',system:{state:'waiting',message:'Works without Codex.'}});
  }
  reconnect(){if(this.state){this.cancelWork();this.update({phase:'offline',message:'Reconnecting…'});this.schedule(0);}else return this.search();}
  configureCodex(enabled,executable='codex'){
    if(!enabled||executable!==this.codexExecutable)this.summary=null;
    this.codexEnabled=!!enabled;this.codexExecutable=executable;this.nextCodex=0;
    if(!enabled)this.update({codex:{state:'off',message:'Agent Dashboard is off. System Monitor stays available.'}});
  }
  async checkCodex(){
    if(!this.codexEnabled||this.codexTask)return;
    const generation=this.generation,executable=this.codexExecutable;
    this.update({codex:{state:'checking',message:'Checking the Codex CLI and its sign-in…'}});this.nextCodex=this.clock()+60000;
    const task=this.collect({executable,signal:this.abort.signal}).then(summary=>{
      if(generation!==this.generation||!this.codexEnabled||executable!==this.codexExecutable)return;
      if(!summary.details&&this.summary?.details)summary.details=this.summary.details;
      this.summary=summary;this.update({codex:{state:'ready',message:'Codex is ready. Usage updates every minute.'}});
    }).catch(e=>{
      if(generation===this.generation&&this.codexEnabled)this.update({codex:{state:'setup',message:e.message+' System Monitor still works.'}});
    }).finally(()=>{if(this.codexTask===task)this.codexTask=null;});
    this.codexTask=task;await task;
  }
  async tick(){
    if(!this.state)return;const generation=this.generation,state=this.state,signal=this.abort.signal;
    try {
      const check=await this.transport.request(state.address,'/v1/status',{token:state.token,signal});if(generation!==this.generation)return;
      if(check.status===403){this.update({phase:'repair',message:'Pairing was removed on your Gadget. Pair this computer again.'});return;}
      if(check.status!==200)throw new Error('Your Gadget is not ready.');
      this.update({phase:'connected',message:'Connected. You can close this window; Gadget Companion stays in the menu bar.'});
      if(this.codexEnabled&&this.clock()>=this.nextCodex)void this.checkCodex();
      if((check.body.appChannels===1||check.body.system===1)){
        const data=await this.collector.sample(this.lastRefresh!==check.body.systemRefresh);this.lastRefresh=check.body.systemRefresh;
        if(generation!==this.generation)return;
        if(data){
          const response=await this.transport.request(state.address,snapshotRoute(check.body,'system-monitor','system','/v1/system'),{token:state.token,data,signal});
          if(generation!==this.generation)return;if(response.status!==200)throw new Error('Your Gadget could not accept the system update.');
          this.update({system:{state:'ready',message:'Sharing system stats every two seconds.'}});
        }else this.update({system:{state:'unsupported',message:'System monitoring currently requires macOS.'}});
      }else this.update({system:{state:'unsupported',message:'Update Gadget OS to enable System Monitor.'}});
      if((check.body.appChannels===1||check.body.activity===1)){
        let data;try{data=await this.activityCollector.sample();}catch{this.update({activity:{message:"Could not read local activity. Check companion folder permissions."}});}
        if(generation!==this.generation)return;
        if(data){
        const response=await this.transport.request(state.address,snapshotRoute(check.body,'agent-usage','activity','/v1/activity'),{token:state.token,data,signal});
        if(generation!==this.generation)return;if(response.status!==200)throw new Error('Your Gadget could not accept the activity update.');
        this.update({activity:{message:data.enabled?`${data.total} sessions · ${data.attention} need attention`:'Enable activity below to set up agent hooks.'}});
        }
      }
      if(this.codexEnabled&&this.summary){
        const response=await this.transport.request(state.address,snapshotRoute(check.body,'agent-usage','usage','/v1/agent-usage'),{token:state.token,data:snapshot(this.summary),signal});
        if(generation!==this.generation)return;if(response.status!==200)throw new Error('Your Gadget could not accept the usage update.');
      }
    }catch(e){
      if(generation!==this.generation)return;
      this.update({phase:'offline',message:'Can’t reach your Gadget. Keep it powered on and connected to the same Wi-Fi. Reconnecting automatically.'});
      try {
        const devices=await this.transport.discover({signal});if(generation!==this.generation)return;
        const match=devices.find(d=>d.id===state.id);
        if(match){const next={...state,address:match.address};await this.persist(async()=>{if(generation===this.generation)await this.storage.save(this.statePath,next);});if(generation!==this.generation)return;this.state=next;this.update({device:this.publicDevice(next)});}
      }catch{}
    }
    if(generation===this.generation)this.schedule(this.view.phase==='offline'?10000:2000);
  }
  stop(){this.cancelWork();}
}
