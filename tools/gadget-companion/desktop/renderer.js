const $=id=>document.getElementById(id);
async function act(name,value){
  $('error').hidden=true;
  try{const result=await window.companion.action(name,value);if(result.error)throw new Error(result.error);return result.value;}
  catch(error){$('error').textContent=error.message;$('error').hidden=false;}
}
function render(s){
  const phase=s.phase;
  const titles={setup:'A little closer to your computer.',searching:'Finding your Gadget…',choose:'Let’s connect your Gadget.',pairing:'One code. One connection.',connected:'Your Gadget is connected.',offline:'We’ll reconnect for you.',repair:'Let’s pair again.'};
  const labels={setup:'Setup required',searching:'Searching',choose:'Choose a Gadget',pairing:'Confirm code',connected:'Connected',offline:'Gadget offline',repair:'Pairing required'};
  $('heading').textContent=titles[phase]||titles.setup;$('message').textContent=s.message;
  $('connection').textContent=labels[phase];$('connection').classList.toggle('ready',phase==='connected');
  $('setup').hidden=!['setup','searching','choose'].includes(phase);$('search').disabled=phase==='searching';$('find-address').disabled=phase==='searching';
  $('devices-card').hidden=phase!=='choose';$('pairing').hidden=phase!=='pairing';$('code').textContent=s.code||'';
  $('connected').hidden=!s.device||['searching','choose','pairing'].includes(phase);
  $('device-name').textContent=s.device?.name||'Your Gadget';$('device-address').textContent=s.device?.address||'';
  $('pair-again').hidden=phase!=='repair';$('reconnect').hidden=phase==='repair';
  $('devices').replaceChildren();
  for(const device of s.devices){
    const button=document.createElement('button');button.className='device';
    const name=document.createElement('span');name.textContent=device.name;
    const address=document.createElement('small');address.textContent=device.address;
    button.append(name,address);button.addEventListener('click',()=>act('pair',device.id));$('devices').append(button);
  }
  $('system-status').textContent=s.system.state==='waiting'?'CPU, memory, network and more. No extra account needed.':s.system.message;
  $('activity-status').textContent=s.activity?.message||'';
  $('codex-activity').checked=!!s.preferences.codexActivity;$('claude-activity').checked=!!s.preferences.claudeActivity;
  $('codex-enabled').checked=s.preferences.codexEnabled;$('codex-status').textContent=s.codex.message;
  $('codex-actions').hidden=!s.preferences.codexEnabled;$('check-codex').disabled=s.codex.state==='checking';
  $('start-at-login').checked=s.preferences.startAtLogin;$('start-at-login').disabled=!s.packaged;$('login-help').hidden=s.packaged;
}
$('search').onclick=()=>act('search');$('search-again').onclick=()=>act('search');
$('find-address').onclick=()=>act('search',$('address').value.trim());
$('address').onkeydown=e=>{if(e.key==='Enter')act('search',$('address').value.trim());};
$('cancel-search').onclick=()=>act('cancel');$('cancel-pairing').onclick=()=>act('cancel');
$('forget').onclick=()=>act('forget');$('reconnect').onclick=()=>act('reconnect');$('pair-again').onclick=()=>act('search');
$('codex-enabled').onchange=()=>act('codex',$('codex-enabled').checked);
$('check-codex').onclick=()=>act('check-codex');$('choose-codex').onclick=()=>act('choose-codex');$('codex-help').onclick=()=>act('help');
$('start-at-login').onchange=async()=>{await act('login',$('start-at-login').checked);const s=await act('status');if(s)render(s);};
for(const provider of ['codex','claude'])$(provider+'-activity').onchange=async()=>{const message=await act('activity',{provider,enabled:$(provider+'-activity').checked});if(message)$('activity-help').textContent=message;const s=await act('status');if(s)render(s);};
$('quit').onclick=()=>act('quit');window.companion.subscribe(render);
act('status').then(s=>{if(s)render(s);});

let historyOffset=0,historyLoading=false;
async function refreshHistory(){
 if(historyLoading)return;historyLoading=true;
 try{
  const result=await window.companion.action('history',historyOffset);if(result.error)throw new Error(result.error);
  const {tasks,total}=result.value;
  $('history-summary').textContent=total?`${historyOffset+1}–${historyOffset+tasks.length} of ${total} tasks`:'No tasks recorded yet. Enable activity and start a new turn.';
  $('history-previous').disabled=historyOffset===0;$('history-next').disabled=historyOffset+tasks.length>=total;
  const expanded=new Set([...$('history-tasks').querySelectorAll('details[open]')].map(row=>row.dataset.id));
  $('history-tasks').replaceChildren();
  const labels={working:'Working',finished:'Turn finished',interrupted:'Interrupted',closed:'Session closed',superseded:'New turn started',disconnected:'No recent events'};
  for(const task of tasks){
   const row=document.createElement('details');row.className='history-task';row.dataset.id=task.id;row.open=expanded.has(task.id);
   const summary=document.createElement('summary');summary.textContent=`${task.project} · ${task.provider?'Claude Code':'Codex'} · ${task.state===1?'Needs attention':labels[task.outcome]||'Unknown'}`;
   const time=document.createElement('p');time.className='muted';time.textContent=`${new Date(task.started).toLocaleString()} · ${Math.floor(task.elapsed/60)}m ${task.elapsed%60}s${task.observedStart?'':' · Start not observed'}`;
   const usage=document.createElement('p');usage.className='history-usage';
   usage.textContent=task.tokens?`${task.tokens.total.toLocaleString()} reported tokens · ${task.tokens.input.toLocaleString()} input + ${task.tokens.output.toLocaleString()} output`:'Tokens: Not reported';
   const detail=document.createElement('p');detail.className='hint';
   detail.textContent=task.tokens?`${task.reportedRequests} reported model requests${task.tokens.cached===null?'':` · ${task.tokens.cached.toLocaleString()} cached input (included)`}${task.tokens.reasoning===null?'':` · ${task.tokens.reasoning.toLocaleString()} reasoning output (included)`}${task.missingRequests?` · ${task.missingRequests} requests without usage`:''}. Counts cover observed requests only.`:'Task history is available even when the agent does not report attributable token usage.';
   row.append(summary,time,usage,detail);$('history-tasks').append(row);
  }
 }catch(error){$('history-summary').textContent='Could not read task history: '+error.message;}
 finally{historyLoading=false;}
}
$('history-refresh').onclick=refreshHistory;
$('history-previous').onclick=()=>{historyOffset=Math.max(0,historyOffset-20);void refreshHistory();};
$('history-next').onclick=()=>{historyOffset+=20;void refreshHistory();};
void refreshHistory();setInterval(()=>{if(!document.hidden)void refreshHistory();},5000);
