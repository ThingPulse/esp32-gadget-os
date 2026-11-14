import os from 'node:os';
import {execFile} from 'node:child_process';
import {promisify} from 'node:util';
import {performance} from 'node:perf_hooks';
const exec=promisify(execFile);
const run=async(file,args=[])=>{try{return (await exec(file,args,{encoding:'utf8',timeout:1200,maxBuffer:2*1024*1024,env:{...process.env,LC_ALL:'C'}})).stdout;}catch{return '';}};
export const cleanName=s=>String(s).replace(/[^\x20-\x7e]/g,'?').slice(0,48);
export function memory(text,total){
 const page=Number(text.match(/page size of (\d+) bytes/)?.[1]);
 const get=k=>{const v=text.match(new RegExp('^'+k+':\\s+(\\d+)','m'));return v?Number(v[1]):NaN;};
 // Anonymous excludes file-backed cache, compressed pages and wired pages.
 const used=(get('Anonymous pages')+get('Pages wired down')+get('Pages occupied by compressor')-get('Pages purgeable'))*page;
 return Number.isFinite(used)&&page>0?Math.min(total,Math.max(0,used)):null;
}
export function cpuTime(s){const m=s.match(/^(?:(\d+)-)?(?:(\d+):)?(\d+):(\d+(?:\.\d+)?)$/);return m?((Number(m[1]||0)*24+Number(m[2]||0))*60+Number(m[3]))*60+Number(m[4]):NaN;}
export function processes(text){return text.split('\n').flatMap(line=>{const m=line.match(/^\s*(\d+)\s+([\d:.\-]+)\s+(\d+)\s+(.+)$/);if(!m)return [];const seconds=cpuTime(m[2]);if(!Number.isFinite(seconds))return [];return [{pid:Number(m[1]),seconds,rss:Number(m[3])*1024,name:cleanName(m[4].split('/').at(-1)).slice(0,32)}];});}
export function network(text,iface){if(!/^en\d+$/.test(iface??''))return null;for(const line of text.split('\n')){const a=line.trim().split(/\s+/);if(a[0]!==iface||!a[2]?.startsWith('<Link#'))continue;const nums=a.slice(3).filter(s=>/^\d+$/.test(s)).map(Number);if(nums.length>=7)return {iface,rx:nums[2],tx:nums[5]};}return null;}
export function battery(text){if(!text)return {battery:-1,power:-1};const m=text.match(/(\d+)%;\s*([^;]+)/);if(!m)return {battery:-1,power:3};return {battery:Math.min(100,Number(m[1])),power:/discharging/i.test(m[2])?0:/^charging$/i.test(m[2].trim())?1:/AC Power/.test(text)?2:0};}
export function disk(text){const line=text.trim().split('\n').at(-1),m=line?.match(/^\S+\s+(\d+)\s+\d+\s+(\d+)\s/);return m?{disk_total_mib:Math.floor(Number(m[1])/1024),disk_free_mib:Math.floor(Number(m[2])/1024)}:{disk_total_mib:-1,disk_free_mib:-1};}
export function cpuDelta(previous,current){if(!previous||!current.length||previous.length!==current.length)return -1;let total=0,idle=0;for(let i=0;i<current.length;i++){for(const key of ['user','nice','sys','idle','irq']){const d=current[i].times[key]-previous[i].times[key];if(d<0)return -1;total+=d;if(key==='idle')idle+=d;}}return total>0?Math.round(100*(total-idle)/total):-1;}
export class SystemCollector {
 constructor({command=run,system=os,clock=()=>performance.now()}={}){this.command=command;this.system=system;this.clock=clock;this.previous=null;this.slow=null;this.slowAt=-Infinity;this.cpuHistory=[];this.ramHistory=[];}
 async sample(force=false){
  if(this.system.platform()!=='darwin')return null;
  const start=this.clock(),os=this.system,cmd=this.command;
  const slow=force||start-this.slowAt>=30000;
  const tasks=[cmd('/usr/bin/vm_stat'),cmd('/bin/ps',['-axo','pid=,time=,rss=,comm=']),cmd('/usr/sbin/netstat',['-ibn'])];
  if(slow)tasks.push(cmd('/usr/bin/pmset',['-g','batt']),cmd('/bin/df',['-k','/System/Volumes/Data']),cmd('/sbin/route',['-n','get','default']),cmd('/usr/sbin/diskutil',['info','/System/Volumes/Data']));
  const [vm,ps,net,batt,df,route,volume]=await Promise.all(tasks);
  if(slow){this.slow={...battery(batt),...disk(df),iface:route.match(/interface:\s*(\S+)/)?.[1],volume:cleanName(volume.match(/^\s*Volume Name:\s*(.+)$/m)?.[1]||'Startup volume')};this.slowAt=start;}
  const cpus=os.cpus(),total=os.totalmem(),used=memory(vm,total),rows=processes(ps),traffic=network(net,this.slow.iface),now=this.clock(),prev=this.previous,seconds=prev?(now-prev.time)/1000:0;
  const valid=seconds>0&&seconds<=6,cpu=valid?cpuDelta(prev.cpus,cpus):-1;
  const ram10=used===null?-1:Math.round(used/total*1000);
  const ram=ram10<0?-1:Math.round(ram10/10);
  const out={schema:1,age:0,host:cleanName(os.hostname()),cpu,cores:cpus.length,ram,ram10,mem_mib:used===null?-1:Math.round(used/1048576),total_mib:Math.round(total/1048576),uptime:Math.floor(os.uptime()),battery:this.slow.battery,power:this.slow.power,disk_total_mib:this.slow.disk_total_mib,disk_free_mib:this.slow.disk_free_mib,volume:this.slow.volume,down_bps:-1,up_bps:-1};
  if(valid&&traffic&&prev.traffic?.iface===traffic.iface){for(const [key,n]of [['down_bps','rx'],['up_bps','tx']]){const d=traffic[n]-prev.traffic[n];if(d>=0)out[key]=Math.min(2147483647,Math.round(d/seconds));}}
  const old=new Map(prev?.rows.map(p=>[p.pid,p])??[]);
  for(const p of rows){const before=old.get(p.pid),d=before&&before.name===p.name?p.seconds-before.seconds:-1;p.cpu=valid&&d>=0&&cpus.length?Math.min(1000,Math.round(d/seconds/cpus.length*1000)):-1;}
  for(const [kind,field]of [['ct','cpu'],['mt','rss']])rows.filter(p=>p[field]>=0).sort((a,b)=>b[field]-a[field]||a.pid-b.pid).slice(0,5).forEach((p,i)=>{out[kind+i+'_name']=p.name;out[kind+i+'_pid']=p.pid;out[kind+i+'_value']=kind==='ct'?p.cpu:Math.min(2147483647,Math.round(p.rss/1048576));});
  const gaps=prev?Math.min(60,Math.max(0,Math.round(seconds/2)-1)):0;
  for(let i=0;i<gaps;i++){this.cpuHistory.push(-1);this.ramHistory.push(-1);}
  this.cpuHistory.push(cpu);this.ramHistory.push(ram10);this.cpuHistory=this.cpuHistory.slice(-60);this.ramHistory=this.ramHistory.slice(-60);
  const encode=a=>a.map(v=>(v<0?255:v).toString(16).padStart(2,'0')).join('');
  // Keep whole-percent history for older apps; one fractional digit per sample
  // adds precision without exceeding the 2 KiB device snapshot limit.
  out.cpu_hist=encode(this.cpuHistory);out.ram_hist=encode(this.ramHistory.map(v=>v<0?-1:Math.floor(v/10)));
  out.ram_frac=this.ramHistory.map(v=>v<0?'f':String(v%10)).join('');
  this.previous={cpus,traffic,rows,time:now};
  if(Buffer.byteLength(JSON.stringify(out))>2048)throw new Error('System snapshot exceeds limit.');
  return out;
 }
}
