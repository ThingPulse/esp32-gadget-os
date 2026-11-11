// Exercise the real Electron window against an isolated, deterministic device.
import {spawn} from 'node:child_process';
import {once} from 'node:events';
import {createServer} from 'node:net';
import {mkdtemp,mkdir,readFile,writeFile,rm} from 'node:fs/promises';
import {tmpdir} from 'node:os';
import {join,resolve} from 'node:path';
import {setTimeout as delay} from 'node:timers/promises';
import assert from 'node:assert/strict';
import {recordEvent} from '../src/activity.mjs';
const work=await mkdtemp(join(tmpdir(),'gadget-desktop-test-'));
const output=resolve('out/screenshots');await mkdir(output,{recursive:true});
const server=createServer();await new Promise(r=>server.listen(0,'127.0.0.1',r));const port=server.address().port;await new Promise(r=>server.close(r));
const binary=process.env.GADGET_COMPANION_BINARY||resolve('node_modules/electron/dist/Electron.app/Contents/MacOS/Electron');
const args=process.env.GADGET_COMPANION_BINARY?[]:['.'];
let log='',child,ws;
try {
  const historyDirectory=join(work,'activity');
  for(let i=0;i<25;i++){
    const base={session_id:'smoke',turn_id:`turn-${i}`,cwd:'/sample/Example project'};
    for(const [j,hook_event_name] of ['UserPromptSubmit','PreModelRequest','PostModelRequest','Stop'].entries()){
      await recordEvent('codex',{...base,hook_event_name,token_usage:{input_tokens:100,output_tokens:20,total_tokens:120,cached_input_tokens:80,reasoning_output_tokens:10}},{directory:historyDirectory,now:Date.now()-100000+i*1000+j});
    }
  }
  const env={...process.env,GADGET_COMPANION_SMOKE:'1',GADGET_COMPANION_TEST_DIR:work};delete env.ELECTRON_RUN_AS_NODE;
  child=spawn(binary,[...args,`--remote-debugging-port=${port}`],{env,stdio:['ignore','pipe','pipe']});
  child.stdout.on('data',b=>{log+=b;});child.stderr.on('data',b=>{log+=b;});
  let target;
  for(let i=0;i<150;++i){
    if(child.exitCode!==null)throw new Error('Electron exited early: '+log);
    try{target=(await(await fetch(`http://127.0.0.1:${port}/json/list`)).json()).find(t=>t.type==='page'&&t.url.endsWith('/index.html'));if(target)break;}catch{}
    await delay(100);
  }
  assert(target,'Electron window did not open: '+log);
  ws=new WebSocket(target.webSocketDebuggerUrl);await new Promise((r,j)=>{ws.onopen=r;ws.onerror=j;});
  let next=0;const pending=new Map();ws.onmessage=event=>{const value=JSON.parse(event.data);const p=pending.get(value.id);if(p){pending.delete(value.id);clearTimeout(p.timer);value.error?p.reject(new Error(value.error.message)):p.resolve(value.result);}};
  const command=(method,params={})=>new Promise((resolve,reject)=>{const id=++next;const timer=setTimeout(()=>{pending.delete(id);reject(new Error('Timed out waiting for '+method));},10000);pending.set(id,{resolve,reject,timer});ws.send(JSON.stringify({id,method,params}));});
  const evaluate=async expression=>{const result=await command('Runtime.evaluate',{expression,awaitPromise:true,returnByValue:true});if(result.exceptionDetails)throw new Error(result.exceptionDetails.exception?.description||result.exceptionDetails.text);return result.result.value;};
  const wait=async expression=>{for(let i=0;i<100;++i){if(await evaluate(expression))return;await delay(50);}throw new Error('Timed out: '+expression+'\n'+log);};
  const capture=async name=>{const {data}=await command('Page.captureScreenshot',{format:'png'});await writeFile(join(output,name+'.png'),Buffer.from(data,'base64'));};
  await wait('!!window.companion && document.getElementById("search")');
  await wait('document.querySelectorAll(".history-task").length === 20');
  assert.match(await evaluate('document.getElementById("history-summary").textContent'),/of 25 tasks/);
  await evaluate('document.querySelector(".history-task").open=true; document.getElementById("task-history").scrollIntoView()');
  assert.match(await evaluate('document.querySelector(".history-usage").textContent'),/120 reported tokens/);
  await capture('task-history');
  await evaluate('document.getElementById("history-next").click()');await wait('document.querySelectorAll(".history-task").length === 5');
  await evaluate('document.getElementById("history-previous").click()');await wait('document.querySelectorAll(".history-task").length === 20');
  await evaluate('window.scrollTo(0,0)');
  assert.equal(await evaluate('typeof require'),'undefined');
  await capture('setup');
  await evaluate('document.getElementById("search").click()');await wait('document.querySelector(".device")');
  await capture('discovery');await evaluate('document.querySelector(".device").click()');
  await wait('!document.getElementById("pairing").hidden');
  assert.match(await evaluate('document.getElementById("code").textContent'),/^[0-9]{6}$/);await capture('pairing');
  await wait('document.getElementById("connection").textContent === "Connected"');
  await capture('connected');
  const pairing=JSON.parse(await readFile(join(work,'pairing.json'),'utf8'));assert.match(pairing.token,/^[a-f0-9]{64}$/);
  assert(!(await evaluate('(async()=>JSON.stringify((await window.companion.action("status")).value))()')).includes(pairing.token));
  await evaluate('document.getElementById("codex-enabled").click()');
  await wait('document.getElementById("codex-status").textContent.includes("not installed")');await capture('codex-setup');
  await evaluate('window.companion.action("forget")');await wait('document.getElementById("connection").textContent === "Setup required"');
  console.log('Checking window close…');
  // Exercise the native close event; DOM window.close also destroys its JS
  // execution context, which is different from clicking the macOS close button.
  await evaluate('window.companion.action("test-close-window")');await delay(300);
  assert.equal(child.exitCode,null,'Closing the window must keep the menu-bar app running');
  const reopen=spawn(binary,args,{env,stdio:'ignore'});await Promise.race([once(reopen,'close'),delay(5000)]);if(reopen.exitCode===null){reopen.kill();throw new Error('Second instance did not exit.');}
  await wait('!!window.companion');
  console.log('Window reopened; checking quit…');
  void evaluate('window.companion.action("quit"); true').catch(()=>{});
  await Promise.race([once(child,'close'),delay(3000)]);
  assert.equal(child.exitCode,0,'Quit must stop the companion');
  console.log('PASS: real Electron window, pairing, private state, optional Codex setup, forget; screenshots in '+output);
} catch(e){console.error(log);throw e;}
finally {
  ws?.close();
  if(child&&child.exitCode===null&&child.signalCode===null){
    const closed=once(child,'close');child.kill();await Promise.race([closed,delay(5000)]);
    if(child.exitCode===null&&child.signalCode===null){child.kill('SIGKILL');await closed;}
  }
  await rm(work,{recursive:true,force:true,maxRetries:10,retryDelay:200});
}
