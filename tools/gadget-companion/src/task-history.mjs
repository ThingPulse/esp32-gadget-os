import {mkdir,readFile,readdir,writeFile,rename,rm} from 'node:fs/promises';
import {join} from 'node:path';
import {createHash,randomUUID} from 'node:crypto';

// One atomic journal per session, updated under recordEvent's session lock.
// No transcript, prompt, model output, raw identifier or account quota is stored.
const hash=value=>createHash('sha256').update(value).digest('hex').slice(0,24);
const identifier=value=>typeof value==='string'&&value.length>0&&value.length<=256?value:null;
const count=value=>Number.isSafeInteger(value)&&value>=0;
export const historyLimit=500;
export const codexEvents=['PreModelRequest','PostModelRequest','Interrupt'];
export function normalizeTokens(value){
 if(!value||!['input_tokens','output_tokens','total_tokens'].every(k=>count(value[k])))return null;
 if(value.input_tokens+value.output_tokens!==value.total_tokens)return null;
 const result={input:value.input_tokens,output:value.output_tokens,total:value.total_tokens};
 for(const [key,target,max] of [['cached_input_tokens','cached',result.input],['reasoning_output_tokens','reasoning',result.output]]){
  if(value[key]!==undefined&&(!count(value[key])||value[key]>max))return null;
  result[target]=value[key]??null;
 }
 return result;
}
async function atomic(path,value){
 const temp=path+'.'+randomUUID()+'.tmp';
 try{await writeFile(temp,JSON.stringify(value),{mode:0o600});await rename(temp,path);}finally{await rm(temp,{force:true}).catch(()=>{});}
}
export async function recordTask(directory,event,next){
 const folder=join(directory,'history');await mkdir(folder,{recursive:true,mode:0o700});
 const path=join(folder,next.id+'.json');let journal={schema:1,tasks:[]};
 try{journal=JSON.parse(await readFile(path,'utf8'));}catch(e){if(e.code!=='ENOENT')throw e;}
 if(journal.schema!==1||!Array.isArray(journal.tasks))throw new Error('Invalid task history');
 const kind=event.hook_event_name,explicit=identifier(next.provider===0?event.turn_id:event.prompt_id);
 // Agent-scoped model requests are separate records, never charged to a parent.
 const agent=identifier(event.agent_id)||'';
 const key=explicit?hash(next.id+':'+explicit+':'+agent):null;
 let task=key?journal.tasks.find(t=>t.id===key):journal.tasks.find(t=>t.id===journal.current);
 const modelEvent=kind==='PreModelRequest'||kind==='PostModelRequest';
 // Without an explicit turn identifier a model request cannot be attributed.
 if(modelEvent&&!key)return null;
 if(kind==='UserPromptSubmit'&&!key)task=null;
 if(!task){
  if(kind==='SessionEnd')return null;
  if(!agent&&!modelEvent){
   const previous=journal.tasks.find(t=>t.id===journal.current);
   if(previous&&previous.state<2){previous.state=3;previous.outcome='superseded';previous.ended=previous.at;}
  }
  task={id:key||randomUUID().replaceAll('-','').slice(0,24),session:next.id,provider:next.provider,project:next.project,
   started:next.at,at:next.at,state:0,event:next.event,outcome:'working',observedStart:kind==='UserPromptSubmit',tokens:null,reportedRequests:0,missingRequests:0,pending:false};
  journal.tasks.push(task);
 }
 if(!agent&&!modelEvent&&(kind==='UserPromptSubmit'||!journal.current))journal.current=task.id;
 if(!modelEvent&&next.at<task.at)return task.id;
 if(kind==='PreModelRequest'){
  if(task.pending)task.missingRequests++;
  task.pending=true;
 }else if(kind==='PostModelRequest'){
  // A paired pre/post prevents duplicate post delivery from counting twice.
  // Missing pre events leave usage unknown rather than guessing ownership.
  if(task.pending){
   task.pending=false;const tokens=normalizeTokens(event.token_usage);
   if(tokens){
    const sum=task.tokens?Object.fromEntries(Object.keys(tokens).map(k=>[k,tokens[k]===null||task.tokens[k]===null?null:tokens[k]+task.tokens[k]])):tokens;
    if(Object.values(sum).every(v=>v===null||count(v))){task.tokens=sum;task.reportedRequests++;}else task.missingRequests++;
   }else task.missingRequests++;
  }
 }else if(kind==='UserPromptSubmit'){
  task.observedStart=true;
 }else if(kind==='Stop'){
  task.state=2;task.outcome='finished';task.ended??=next.at;
 }else if(kind==='Interrupt'||kind==='SessionEnd'){
  if(task.state!==2){task.state=3;task.outcome=kind==='Interrupt'?'interrupted':'closed';task.ended=next.at;}
 }else if(task.state!==2){
  task.state=next.state;task.outcome='working';delete task.ended;
 }
 if(!modelEvent){task.at=next.at;task.event=next.event;}
 else if(task.state<2)task.at=Math.max(task.at,next.at);
 journal.tasks.sort((a,b)=>b.started-a.started);
 journal.tasks=journal.tasks.slice(0,historyLimit);
 await atomic(path,journal);return task.id;
}
export async function readTaskHistory(directory,{now=Date.now(),offset=0,limit=20}={}){
 const folder=join(directory,'history');let files;
 try{files=await readdir(folder);}catch(e){if(e.code==='ENOENT')return {tasks:[],total:0};throw e;}
 const tasks=[];
 for(const file of files){
  if(!/^[a-f0-9]{24}\.json$/.test(file))continue;
  try{
   const journal=JSON.parse(await readFile(join(folder,file),'utf8'));
   if(journal.schema!==1||!Array.isArray(journal.tasks))continue;
   for(const t of journal.tasks.slice(0,historyLimit)){
    if(!/^[a-f0-9]{24}$/.test(t.id)||![0,1].includes(t.provider)||![0,1,2,3].includes(t.state)||!count(t.started)||!count(t.at)||t.at<t.started||t.started>now)continue;
    const stale=t.state<2&&now-t.at>900000;
    tasks.push({id:t.id,session:t.session,provider:t.provider,project:t.project,started:t.started,at:t.at,
     state:stale?3:t.state,outcome:stale?'disconnected':t.outcome,event:t.event,
     elapsed:Math.min(604800,Math.max(0,Math.floor(((t.ended??(stale?t.at:now))-t.started)/1000))),
     tokens:t.tokens,reportedRequests:t.reportedRequests,missingRequests:t.missingRequests,
     observedStart:t.observedStart,pending:t.pending});
   }
  }catch{/* One damaged journal must not hide the other sessions. */}
 }
 tasks.sort((a,b)=>b.started-a.started||a.id.localeCompare(b.id));
 return {tasks:tasks.slice(offset,offset+limit),total:tasks.length};
}
