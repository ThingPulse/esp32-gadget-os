import {readdir,readFile,rm,mkdir,writeFile,rename,stat} from 'node:fs/promises';
import {join,basename} from 'node:path';
import {recordTask,readTaskHistory,codexEvents} from './task-history.mjs';
import {compactCount} from './codex.mjs';
import {homedir} from 'node:os';
import {createHash,randomUUID} from 'node:crypto';
export const activityDirectory=()=>process.env.GADGET_COMPANION_ACTIVITY_DIR||join(homedir(),'.gadget-companion','activity');
export const events=['UserPromptSubmit','PreToolUse','PermissionRequest','PostToolUse','Stop','SessionEnd'];
const clean=(s,n=48)=>String(s??'').replace(/[^\x20-\x7e]/g,'?').slice(0,n);
// Deliberately discard prompts, arguments, output, credentials and full paths.
export function normalizeEvent(provider,event,now=Date.now()){
 if(!['codex','claude'].includes(provider)||![...events,...(provider==='codex'?codexEvents:[])].includes(event?.hook_event_name)||typeof event.session_id!=='string'||!event.session_id||event.session_id.length>256)return null;
 const kind=event.hook_event_name;
 const rawTurn=provider==='codex'?event.turn_id:event.prompt_id;
 const turn=typeof rawTurn==='string'&&rawTurn.length>0&&rawTurn.length<=256?createHash('sha256').update(rawTurn+':'+String(event.agent_id||'')).digest('hex').slice(0,24):null;
 return {id:createHash('sha256').update(provider+':'+event.session_id).digest('hex').slice(0,24),provider:provider==='codex'?0:1,
  turn,project:clean(basename(String(event.cwd||'Project').replaceAll('\\','/')))||'Project',
  state:kind==='PermissionRequest'?1:kind==='Stop'?2:kind==='SessionEnd'||kind==='Interrupt'?3:0,
  event:kind==='PermissionRequest'?'Approval requested':kind==='Stop'?'Ready to review':kind==='SessionEnd'?'Session closed':kind==='Interrupt'?'Turn interrupted':kind==='UserPromptSubmit'?'Processing request':kind==='PreToolUse'?'Using '+clean(event.tool_name||'tool',32):'Processing results',
  at:now,started:now,newTurn:kind==='UserPromptSubmit',kind,tool:createHash('sha256').update(String(event.tool_use_id||event.tool_name||'tool')).digest('hex').slice(0,16)};
}
export async function recordEvent(provider,event,{directory=activityDirectory(),now=Date.now()}={}){
 const next=normalizeEvent(provider,event,now);if(!next)return;
 await mkdir(directory,{recursive:true,mode:0o700});const path=join(directory,next.id+'.json'),lock=path+'.lock';
 // Parallel tool hooks share a session. Serialize their tiny state updates so an
 // unrelated tool completion cannot hide an outstanding approval request.
 let acquired=false;
 for(let i=0;i<60&&!acquired;i++){try{await mkdir(lock);acquired=true;}catch(e){if(e.code!=='EEXIST')throw e;try{if(Date.now()-(await stat(lock)).mtimeMs>5000)await rm(lock,{recursive:true,force:true});}catch{}await new Promise(r=>setTimeout(r,10));}}
 if(!acquired)return;
 try{
  let old={};try{old=JSON.parse(await readFile(path,'utf8'));}catch{}
  if(['PreModelRequest','PostModelRequest'].includes(next.kind)){await recordTask(directory,event,next);return;}
  if(old.at>now)return;
  const sameTurn=!next.turn||!old.turn||next.turn===old.turn;
  if(sameTurn&&!next.newTurn&&Number.isFinite(old.started)&&old.started<=now)next.started=old.started;
  const approvals=new Set(sameTurn&&Array.isArray(old.approvals)?old.approvals:[]);
  if(next.newTurn||next.state>=2)approvals.clear();
  else if(next.kind==='PermissionRequest')approvals.add(next.tool);
  else if(next.kind==='PostToolUse')approvals.delete(next.tool);
  if(approvals.size){next.state=1;next.event='Approval requested';}
  next.approvals=[...approvals].slice(0,16);
  next.taskId=await recordTask(directory,event,next);
  delete next.newTurn;delete next.kind;delete next.tool;
  const temp=path+'.'+randomUUID()+'.tmp';
  try{await writeFile(temp,JSON.stringify(next),{mode:0o600});await rename(temp,path);}finally{await rm(temp,{force:true}).catch(()=>{});}
 }finally{await rm(lock,{recursive:true,force:true});}
}
export class ActivityCollector {
 constructor({directory=activityDirectory(),clock=Date.now}={}){this.directory=directory;this.clock=clock;}
 async sample(){
  const now=this.clock(),sessions=[];let files=[];
  try{files=await readdir(this.directory);}catch(e){if(e.code!=='ENOENT')throw e;}
  for(const file of files){
   if(!/^[a-f0-9]{24}\.json$/.test(file))continue;
   const path=join(this.directory,file);
   try{
    const raw=await readFile(path,'utf8');if(raw.length>4096)continue;const s=JSON.parse(raw);
    if(!Number.isFinite(s.at)||!Number.isFinite(s.started)||s.at>now||s.started>s.at||!Number.isInteger(s.state)||s.state<0||s.state>3||![0,1].includes(s.provider))continue;
    const age=Math.floor((now-s.at)/1000);if(age>86400){await rm(path,{force:true});continue;}
    // Hooks have no heartbeat: silence must never imply completion.
    sessions.push({...s,id:file.slice(0,-5),project:clean(s.project)||'Project',event:age>900?'No recent agent events':clean(s.event)||'Updated',state:age>900?3:s.state,age,elapsed:Math.min(604800,Math.floor(((s.state<2?now:s.at)-s.started)/1000))});
   }catch{}
  }
  const history=await readTaskHistory(this.directory,{now,limit:Number.MAX_SAFE_INTEGER});
  const tracked=new Set(history.tasks.map(t=>t.session));
  for(let i=sessions.length-1;i>=0;i--)if(tracked.has(sessions[i].id))sessions.splice(i,1);
  for(const t of history.tasks)sessions.push({...t,age:Math.min(604800,Math.max(0,Math.floor((now-t.at)/1000)))});
  sessions.sort((a,b)=>({1:0,0:1,2:2,3:3}[a.state]-{1:0,0:1,2:2,3:3}[b.state])||(a.state<2?a.project.localeCompare(b.project):0)||b.at-a.at);
  let providers={};try{providers=JSON.parse(await readFile(join(this.directory,'.providers.json'),'utf8'));}catch{}
  const result={schema:1,age:0,enabled:sessions.length||providers?.codex===true||providers?.claude===true?1:0,count:Math.min(6,sessions.length),total:Math.min(10000,sessions.length),attention:Math.min(10000,sessions.filter(s=>s.state===1).length)};
  sessions.slice(0,6).forEach((s,i)=>{for(const k of ['id','project','event','state','provider','age','elapsed'])result[`s${i}_${k}`]=s[k];const tokens=compactCount(s.tokens?.total);if(tokens){result[`s${i}_tokens_v`]=tokens.v;result[`s${i}_tokens_u`]=tokens.u;}});
  // Preserve the firmware's 2 KiB wire bound even with escaped project names.
  while(Buffer.byteLength(JSON.stringify(result))>2048){
   let shortened=false;for(let i=0;i<result.count;i++)for(const k of ['event','project']){const key=`s${i}_${k}`;if(result[key].length>12){result[key]=result[key].slice(0,-1);shortened=true;}}
   if(!shortened)throw new Error('Activity snapshot exceeds its wire limit');
  }
  return result;
 }
}
