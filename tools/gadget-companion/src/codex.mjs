import {spawn, execFileSync} from 'node:child_process';
import {existsSync} from 'node:fs';
import {dirname, join} from 'node:path';

// npm's Windows .cmd wrapper cannot be spawned directly without a shell.
// Resolve its adjacent official JS entry and invoke Node directly instead.
export function codexCommand(executable, platform=process.platform, find=path=>execFileSync('where.exe',[path],{encoding:'utf8',windowsHide:true}), exists=existsSync) {
  if(platform!=='win32')return {file:executable,args:['app-server']};
  let resolved=executable;
  try{resolved=find(executable).trim().split(/\r?\n/).find(p=>/\.(exe|cmd|bat)$/i.test(p))||executable;}catch{}
  if(/\.(cmd|bat)$/i.test(resolved)){
    const entry=join(dirname(resolved),'node_modules','@openai','codex','bin','codex.js');
    if(!exists(entry))throw new Error('Use --codex with the native codex.exe path, or install the official Codex npm package.');
    return {file:process.execPath,args:[entry,'app-server']};
  }
  return {file:resolved,args:['app-server']};
}

const number=(v,low,high)=>typeof v==='number'&&Number.isFinite(v)&&v>=low&&v<=high;
export function normalize(result, now=Math.floor(Date.now()/1000)) {
  const buckets=result?.rateLimitsByLimitId;
  const bucket=buckets && Object.keys(buckets).length ? buckets.codex : result?.rateLimits;
  if (!bucket || typeof bucket!=='object') throw new Error('Codex quota is unavailable. Check your Codex login.');
  return {observed:now,windows:['primary','secondary'].map(key=>{
    const w=bucket[key];
    return w && number(w.usedPercent,0,100) && number(w.resetsAt,0,4102444800) && number(w.windowDurationMins,1,525600)
      ? {used:w.usedPercent,reset:Math.floor(w.resetsAt),minutes:Math.floor(w.windowDurationMins)} : null;
  })};
}
// Compact counts keep the freestanding app within its int32 JSON ABI while
// representing large lifetime totals. Unit 0 is exact; K/M/B/T use one decimal.
export function compactCount(value) {
  if(!Number.isSafeInteger(value)||value<0)return null;
  let unit=0,divisor=1;
  while(value/divisor>=1000&&unit<4){unit++;divisor*=1000;}
  if(!unit)return {v:value,u:0};
  let v=Math.round(value/divisor*10);
  if(v>=10000&&unit<4){unit++;v=Math.round(v/1000);}
  return v<=9999?{v,u:unit}:null;
}
export function normalizeUsage(result,now=Math.floor(Date.now()/1000)) {
  if(!result || typeof result!=='object')return null;
  const raw=result.summary??{},summary={};
  for(const key of ['lifetimeTokens','peakDailyTokens','currentStreakDays','longestStreakDays','longestRunningTurnSec'])
    summary[key]=Number.isSafeInteger(raw[key])&&raw[key]>=0?raw[key]:null;
  const days={};
  for(const b of Array.isArray(result.dailyUsageBuckets)?result.dailyUsageBuckets:[]){
    if(typeof b?.startDate!=='string'||!/^\d{4}-\d{2}-\d{2}$/.test(b.startDate)||!Number.isSafeInteger(b.tokens)||b.tokens<0)continue;
    const timestamp=Date.parse(b.startDate+'T00:00:00Z');
    if(!Number.isFinite(timestamp)||new Date(timestamp).toISOString().slice(0,10)!==b.startDate)continue;
    // Retain only the recent month; no arbitrary history enters the device feed.
    if(timestamp/1000<=now&&timestamp/1000>now-32*86400)days[b.startDate]=b.tokens;
  }
  return {observed:now,summary,days};
}
export function snapshot(summary, now=Math.floor(Date.now()/1000)) {
  const age=summary && number(summary.observed,0,now)?Math.min(604800,now-summary.observed):-1;
  const out={schema:1,generated_at:now,codex_enabled:1,claude_enabled:0,codex_age:age,claude_age:-1};
  for (const [i,name] of ['primary','secondary'].entries()) {
    const key='codex_'+name,w=summary?.windows?.[i];out[key+'_used']=-1;
    if(age>=0 && w && w.reset>now){out[key+'_used']=w.used;out[key+'_minutes']=w.minutes;out[key+'_reset']=Math.min(31536000,w.reset-now);}
  }
  const detail=summary?.details;
  out.codex_detail_age=detail&&number(detail.observed,0,now)?Math.min(604800,now-detail.observed):-1;
  const putCount=(prefix,value)=>{const c=compactCount(value);if(c){out[prefix+'_v']=c.v;out[prefix+'_u']=c.u;}};
  if(out.codex_detail_age>=0){
    putCount('codex_lifetime',detail.summary.lifetimeTokens);putCount('codex_peak',detail.summary.peakDailyTokens);
    for(const [source,key,max] of [['currentStreakDays','codex_streak',36500],['longestStreakDays','codex_best_streak',36500],['longestRunningTurnSec','codex_longest_sec',31536000]]){
      const v=detail.summary[source];if(number(v,0,max))out[key]=v;
    }
  }
  const midnight=Math.floor(now/86400)*86400,days=[];
  for(let i=0;i<7;i++){
    const date=new Date((midnight-(6-i)*86400)*1000).toISOString().slice(0,10);
    days.push({date,value:out.codex_detail_age>=0?detail.days[date]:null});
  }
  const max=Math.max(1,...days.map(d=>d.value??0));
  for(const [i,d]of days.entries()){
    const key='codex_d'+i;out[key+'_date']=Number(d.date.slice(5,7))*100+Number(d.date.slice(8));
    putCount(key,d.value);
    if(Number.isSafeInteger(d.value)&&d.value>=0)out[key+'_bar']=d.value?Math.max(1,Math.round(d.value/max*100)):0;
  }
  // Calendar periods are UTC. Missing buckets remain unknown, never zero.
  const today=new Date(midnight*1000),monthStart=Date.UTC(today.getUTCFullYear(),today.getUTCMonth(),1)/1000;
  const getDay=t=>out.codex_detail_age>=0?detail.days[new Date(t*1000).toISOString().slice(0,10)]:undefined;
  const aggregate=(start,end)=>{let total=0,known=0,expected=0;for(let t=start;t<=end;t+=86400){expected++;const v=getDay(t);if(Number.isSafeInteger(v)&&v>=0){total+=v;known++;}}return {total,known,expected};};
  const periods=[aggregate(midnight,midnight),aggregate(midnight-6*86400,midnight),aggregate(monthStart,midnight)];
  periods.forEach((p,i)=>{const key='cx_p'+i;out[key+'_n']=p.known;out[key+'_e']=p.expected;if(p.known)putCount(key,p.total);});
  const month=[];
  for(let i=0;i<7;i++)month.push(aggregate(monthStart+i*5*86400,Math.min(midnight,monthStart+(i*5+4)*86400)));
  const peak=Math.max(1,...month.map(p=>p.total));
  month.forEach((p,i)=>{if(!p.known)return;const key='cx_m'+i;putCount(key,p.total);out[key+'_bar']=p.total?Math.max(1,Math.round(p.total/peak*100)):0;});
  return out;
}
export function collectCodex({executable='codex',timeout=20000,spawnProcess=spawn,signal}={}) {
  return new Promise((resolve,reject)=>{
    if(signal?.aborted){reject(new Error('Stopped.'));return;}
    // Use the existing CLI login. Never open auth.json or start an agent turn.
    const command=codexCommand(executable);
    const child=spawnProcess(command.file,command.args,{stdio:['pipe','pipe','ignore'],windowsHide:true});
    let buffer='',done=false,initialized=false,quota=null;let usageTimer;
    const timer=setTimeout(()=>quota?finish(null,quota):finish(new Error('Codex timed out; check its login and version.')),timeout);
    function finish(error,result){
      if(done)return;done=true;clearTimeout(timer);clearTimeout(usageTimer);signal?.removeEventListener('abort',abort);child.stdin.end();child.kill();
      const killTimer=setTimeout(()=>child.kill('SIGKILL'),2000);killTimer.unref();child.once('close',()=>clearTimeout(killTimer));
      error?reject(error):resolve(result);
    }
    const abort=()=>finish(new Error('Stopped.'));signal?.addEventListener('abort',abort,{once:true});
    function send(message){if(!done)child.stdin.write(JSON.stringify(message)+'\n');}
    child.once('error',()=>finish(new Error('Cannot start Codex. Install the Codex CLI and sign in first.')));
    child.stdin.on('error',()=>finish(new Error('Codex connection closed.')));
    child.once('exit',()=>{if(!done){if(quota)finish(null,quota);else finish(new Error('Codex closed before returning usage.'));}});
    child.stdout.setEncoding('utf8');
    child.stdout.on('data',chunk=>{
      buffer+=chunk;if(buffer.length>1048576){finish(new Error('Oversized Codex response.'));return;}
      let end;while(!done&&(end=buffer.indexOf('\n'))>=0){
        const line=buffer.slice(0,end);buffer=buffer.slice(end+1);let m;try{m=JSON.parse(line);}catch{continue;}
        if(!m||typeof m!=='object')continue;
        if(m.method&&m.id!==undefined){send({id:m.id,error:{code:-32601,message:'Unsupported'}});continue;}
        if(m.method)continue;
        if((m.id===1||m.id===2)&&m.error){finish(new Error('Codex usage is unavailable. Check the CLI login/version.'));return;}
        if(m.id===1&&!initialized){initialized=true;send({method:'initialized',params:{}});send({id:2,method:'account/rateLimits/read'});}
        else if(m.id===2&&initialized){
          try{quota=normalize(m.result);send({id:3,method:'account/usage/read'});
            // Optional endpoint: older CLIs or a slow service must not break quotas.
            usageTimer=setTimeout(()=>finish(null,quota),5000);
          }catch(e){finish(e);}
        }else if(m.id===3&&quota){quota.details=m.error?null:normalizeUsage(m.result);finish(null,quota);}
      }
    });
    send({id:1,method:'initialize',params:{clientInfo:{name:'gadget_companion',version:'0.1.0'}}});
  });
}
