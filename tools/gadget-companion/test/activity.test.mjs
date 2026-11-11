import {test} from 'node:test';
import assert from 'node:assert/strict';
import {mkdtemp,readFile,readdir,rm,writeFile,mkdir} from 'node:fs/promises';
import {join} from 'node:path';
import {tmpdir} from 'node:os';
import {ActivityCollector,recordEvent,normalizeEvent,events} from '../src/activity.mjs';
import {configureActivity} from '../src/activity-setup.mjs';
async function temporary(fn){const directory=await mkdtemp(join(tmpdir(),'gadget-activity-'));try{await fn(directory);}finally{await rm(directory,{recursive:true,force:true});}}
const event=(hook_event_name,session_id='a')=>({hook_event_name,session_id,cwd:'/private/projects/gadget-os',tool_name:'Bash',prompt:'SECRET',tool_input:{command:'SECRET'},last_assistant_message:'SECRET'});
test('activity follows agent lifecycle without retaining conversation content',()=>temporary(async directory=>{
 let now=100000;const collector=new ActivityCollector({directory,clock:()=>now});
 assert.equal((await collector.sample()).count,0);
 await recordEvent('codex',event('UserPromptSubmit'),{directory,now});now+=2000;
 await recordEvent('codex',event('PermissionRequest'),{directory,now});
 let s=await collector.sample();assert.equal(s.s0_state,1);assert.equal(s.attention,1);assert.equal(s.s0_elapsed,2);assert.equal(s.s0_project,'gadget-os');
 assert(!JSON.stringify(s).includes('SECRET'));assert(!JSON.stringify(s).includes('/private'));
 const raw=await readFile(join(directory,(await readdir(directory))[0]),'utf8');assert(!raw.includes('SECRET'));
 now+=3000;await recordEvent('codex',event('PostToolUse'),{directory,now});s=await collector.sample();assert.equal(s.s0_state,0);assert.equal(s.attention,0);
 now+=5000;await recordEvent('codex',event('Stop'),{directory,now});now+=20000;s=await collector.sample();assert.equal(s.s0_state,2);assert.equal(s.s0_elapsed,10);
 await recordEvent('codex',event('SessionEnd'),{directory,now});assert.equal((await collector.sample()).s0_state,2);
 now+=86401000;assert.equal((await collector.sample()).count,1);assert.deepEqual(await readdir(directory),['history']);
}));
test('six-session wire limit prioritizes attention and expires silence without claiming success',()=>temporary(async directory=>{
 let now=100000;const collector=new ActivityCollector({directory,clock:()=>now});
 for(let i=0;i<10;i++)await recordEvent(i%2?'claude':'codex',event(i===9?'PermissionRequest':'UserPromptSubmit',String(i)),{directory,now});
 const s=await collector.sample();assert.equal(s.total,10);assert.equal(s.count,6);assert.equal(s.s0_state,1);assert(Buffer.byteLength(JSON.stringify(s))<=2048);
 now+=901000;const stale=await collector.sample();assert.equal(stale.attention,0);assert.equal(stale.s0_state,3);
 assert.equal(normalizeEvent('other',event('Stop')),null);assert.equal(normalizeEvent('codex',event('Unknown')),null);
}));
test('hook setup preserves other settings, is idempotent, backs up, and removes only companion hooks',()=>temporary(async home=>{
 const path=join(home,'.claude','settings.json');await mkdir(join(home,'.claude'));
 await writeFile(path,JSON.stringify({theme:'dark',hooks:{Stop:[{hooks:[{type:'command',command:'my-other-hook'}]}]}}));
 const options={home,executable:'/Applications/My App.app/Contents/MacOS/App',script:"/tmp/test's helper.mjs",electron:true};
 await configureActivity('claude',true,options);await configureActivity('claude',true,options);
 const c=JSON.parse(await readFile(path));assert.equal(c.theme,'dark');assert.equal(c.hooks.Stop.length,2);assert.equal(Object.keys(c.hooks).length,events.length);
 assert(c.hooks.Stop[1].hooks[0].command.includes('ELECTRON_RUN_AS_NODE=1'));
 await configureActivity('claude',false,options);const off=JSON.parse(await readFile(path));assert.equal(off.hooks.Stop.length,1);assert.equal(off.hooks.Stop[0].hooks[0].command,'my-other-hook');
 await writeFile(path,'invalid');await assert.rejects(configureActivity('claude',true,options),/left unchanged/);assert.equal(await readFile(path,'utf8'),'invalid');
}));
test('parallel tools cannot clear another tool approval; new turns reset elapsed time',()=>temporary(async directory=>{
 const write=(kind,id,now)=>recordEvent('codex',{...event(kind),tool_use_id:id},{directory,now});
 await write('UserPromptSubmit','',1000);
 await write('PermissionRequest','a',2000);
 await Promise.all([write('PostToolUse','b',3000),write('PreToolUse','c',3000)]);
 let sample=await new ActivityCollector({directory,clock:()=>4000}).sample();assert.equal(sample.s0_state,1);
 await write('PostToolUse','a',5000);sample=await new ActivityCollector({directory,clock:()=>6000}).sample();assert.equal(sample.s0_state,0);
 await write('Stop','',7000);await write('UserPromptSubmit','',8000);sample=await new ActivityCollector({directory,clock:()=>9000}).sample();assert.equal(sample.s0_elapsed,1);
}));
