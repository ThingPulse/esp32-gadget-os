import {test} from 'node:test';
import assert from 'node:assert/strict';
import {mkdtemp,rm,readFile,readdir,stat} from 'node:fs/promises';
import {join} from 'node:path';
import {tmpdir} from 'node:os';
import {recordEvent,ActivityCollector} from '../src/activity.mjs';
import {readTaskHistory,normalizeTokens,historyLimit} from '../src/task-history.mjs';
const usage={input_tokens:100,output_tokens:20,total_tokens:120,cached_input_tokens:80,reasoning_output_tokens:10};
async function temporary(fn){const directory=await mkdtemp(join(tmpdir(),'task-history-'));try{await fn(directory);}finally{await rm(directory,{recursive:true,force:true});}}
function writer(directory){let now=10000;return (kind,turn='t1',extra={},provider='codex')=>recordEvent(provider,{hook_event_name:kind,session_id:'secret-session',turn_id:turn,prompt_id:turn,cwd:'/private/secret/project',prompt:'SECRET',token_usage:usage,...extra},{directory,now:now+=1000});}
test('durable distinct turns, reported tokens, restart, privacy and no cache/reasoning double count',()=>temporary(async directory=>{
 const write=writer(directory);
 await write('UserPromptSubmit');await write('PreModelRequest');await write('PostModelRequest');
 await write('PostModelRequest'); // duplicate post must not count twice
 await write('PreModelRequest');await write('PostModelRequest');await write('Stop');
 await write('UserPromptSubmit','t2');await write('Stop','t2');
 const result=await readTaskHistory(directory,{now:864000000});
 assert.equal(result.total,2);assert.equal(result.tasks[0].tokens,null);
 assert.deepEqual(result.tasks[1].tokens,{input:200,output:40,total:240,cached:160,reasoning:20});
 assert.equal(result.tasks[1].state,2);assert.equal(result.tasks[1].elapsed,6);
 assert.equal(result.tasks[1].reportedRequests,2);
 const sample=await new ActivityCollector({directory,clock:()=>864000000}).sample();assert.equal(sample.count,2);assert.equal(sample.s1_tokens_v,240);
 const folder=join(directory,'history');const file=join(folder,(await readdir(folder))[0]);
 const raw=await readFile(file,'utf8');for(const secret of ['SECRET','/private','secret-session','token_usage','prompt'])assert(!raw.includes(secret));
 assert.equal((await stat(file)).mode&0o777,0o600);
}));
test('missing or malformed usage remains unknown; zero is a valid report; cache is a subset',()=>temporary(async directory=>{
 const write=writer(directory);await write('UserPromptSubmit');
 await write('PreModelRequest');await write('PostModelRequest','t1',{token_usage:null});
 await write('PreModelRequest');await write('PostModelRequest','t1',{token_usage:{...usage,input_tokens:-1}});
 let task=(await readTaskHistory(directory,{now:100000})).tasks[0];assert.equal(task.tokens,null);assert.equal(task.missingRequests,2);
 await write('PreModelRequest');await write('PostModelRequest','t1',{token_usage:{input_tokens:0,output_tokens:0,total_tokens:0}});
 task=(await readTaskHistory(directory,{now:100000})).tasks[0];assert.equal(task.tokens.total,0);assert.equal(task.tokens.cached,null);
 assert.equal(normalizeTokens({...usage,cached_input_tokens:101}),null);
 assert.equal(normalizeTokens({...usage,total_tokens:140}),null);
 assert.equal(normalizeTokens({...usage,output_tokens:NaN}),null);
}));
test('parallel sessions and subagents never attribute usage to the parent or another task',()=>temporary(async directory=>{
 const write=writer(directory);await write('UserPromptSubmit');await write('PreModelRequest');
 await write('PreModelRequest','t1',{agent_id:'child'});await write('PostModelRequest','t1',{agent_id:'child'});
 await write('PreModelRequest','t1',{session_id:'other'});await write('PostModelRequest','t1',{session_id:'other'});
 await write('PostModelRequest');await write('Stop');
 const result=await readTaskHistory(directory,{now:100000});assert.equal(result.total,3);assert(result.tasks.every(t=>t.tokens.total===120));
}));
test('late usage updates a finished turn without reopening it; absent identity cannot receive tokens',()=>temporary(async directory=>{
 const write=writer(directory);await write('UserPromptSubmit');await write('PreModelRequest');await write('Stop');
 await write('UserPromptSubmit','t2');await write('PostModelRequest','t1');
 await write('PostModelRequest','',{token_usage:usage});
 const result=await readTaskHistory(directory,{now:100000});assert.equal(result.total,2);assert.equal(result.tasks[0].tokens,null);assert.equal(result.tasks[1].tokens.total,120);assert.equal(result.tasks[1].state,2);
}));
test('Claude history works without usage; interrupted and stale turns never claim success',()=>temporary(async directory=>{
 const write=writer(directory);await write('UserPromptSubmit','c',{},'claude');await write('Stop','c',{},'claude');
 await write('UserPromptSubmit');await write('Interrupt');await write('UserPromptSubmit','t2');
 const result=await readTaskHistory(directory,{now:10000000});assert.equal(result.total,3);
 assert.equal(result.tasks[0].outcome,'disconnected');assert.equal(result.tasks[1].outcome,'interrupted');assert.equal(result.tasks[2].state,2);assert.equal(result.tasks[2].tokens,null);
}));
test('pagination and bounded per-session retention preserve latest turns',()=>temporary(async directory=>{
 const write=writer(directory);
 for(let i=0;i<historyLimit+2;i++){await write('UserPromptSubmit','t'+i);await write('Stop','t'+i);}
 const page=await readTaskHistory(directory,{now:10000000,offset:20,limit:20});assert.equal(page.total,historyLimit);assert.equal(page.tasks.length,20);assert(page.tasks[0].started>page.tasks[19].started);
}));
test('maximum escaped labels and token fields fit firmware wire bound',()=>temporary(async directory=>{
 const write=writer(directory);
 for(let i=0;i<6;i++){
  await write('UserPromptSubmit','t'+i,{cwd:'/'+ '"'.repeat(48)});await write('PreModelRequest','t'+i);await write('PostModelRequest','t'+i);await write('Stop','t'+i);
 }
 const sample=await new ActivityCollector({directory,clock:()=>100000}).sample();assert.equal(sample.count,6);assert(Buffer.byteLength(JSON.stringify(sample))<=2048);
}));
