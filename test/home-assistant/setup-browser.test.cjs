// Execute the real embedded page script with a minimal DOM and HTTP stub.
// No browser package or external service is needed for pairing regressions.
const fs = require('node:fs');
const vm = require('node:vm');
const assert = require('node:assert/strict');
const html = fs.readFileSync('src/services/HomeAssistantSetup.html', 'utf8');
const script = html.split('<script>')[1].split('</script>')[0];
const A = 'a'.repeat(32), B = 'b'.repeat(32);
function browser(fragment='', remembered='', blockedStorage=false) {
 const nodes = new Map(), listeners = {}, requests = [], storage = new Map([['home-setup-key', remembered]]);
 function element(tag='div') {
  const classes = new Set();
  return {tag, value:'', textContent:'', dataset:{}, children:[], disabled:false,
   classList:{add:c=>classes.add(c),remove:c=>classes.delete(c),contains:c=>classes.has(c),toggle:(c,on)=>on?classes.add(c):classes.delete(c)},
   append(...items){this.children.push(...items)},replaceChildren(...items){this.children=items},showModal(){},close(){}};
 }
 for (const match of html.matchAll(/\bid="([^"]+)"/g)) nodes.set(match[1],element());
 nodes.get('editor').classList.add('hidden');
 const nav=[1,2,3].map(i=>{const e=element('button');e.dataset.step=String(i);return e});
 const location={hash:fragment, pathname:'/', origin:'http://device:8080'};
 let accepted=A;
 const context=vm.createContext({console, URL, Blob, setTimeout, clearTimeout, location,
  history:{replaceState(_,__,url){location.hash=url.includes('#')?'#'+url.split('#')[1]:''}},
  sessionStorage:{getItem:k=>{if(blockedStorage)throw Error('Blocked');return storage.get(k)},setItem:(k,v)=>{if(blockedStorage)throw Error('Blocked');storage.set(k,v)},removeItem:k=>storage.delete(k)},
  document:{getElementById:id=>nodes.get(id),createElement:element,querySelectorAll:s=>s==='nav button'||s==='[data-step]'?nav:[]},
  window:{addEventListener:(event,fn)=>listeners[event]=fn},
  fetch:async(path,options)=>{
   requests.push({path,options});const valid=options.headers['X-Setup-Key']===accepted;
   return {ok:valid,status:valid?200:403,json:async()=>valid?(path==='/api/config'&&options.method==='GET'?{url:'http://ha:8123',ca:'',has_token:false,pages:[{name:'Home',tiles:[]}]}:{ok:true}):{error:'Open setup on the device and use its link'}};
  }
 });
 vm.runInContext(script,context);
 return {nodes,requests,location,storage,context,listeners,accept:key=>accepted=key,eval:s=>vm.runInContext(s,context)};
}
async function settled(b){for(let i=0;i<100;i++){await new Promise(r=>setImmediate(r));if(!b.eval('working'))return}throw Error('Page still busy')}
(async()=>{
 const missing=browser();await settled(missing);
 assert.equal(missing.requests.length,0);assert(missing.nodes.get('editor').classList.contains('hidden'));
 assert.match(missing.nodes.get('message').textContent,/complete current setup link/);
 missing.nodes.get('setupLink').value='http://device:8080/#'+A;missing.nodes.get('pair').onclick();await settled(missing);
 assert(!missing.nodes.get('editor').classList.contains('hidden'));assert.equal(missing.location.hash,'#'+A);
 // Copying the address into a different browser carries authentication with it.
 const copied=browser(missing.location.hash);await settled(copied);assert(copied.eval('paired'));
 // A new QR code in the same tab replaces the old key, without losing edits.
 copied.nodes.get('url').value='http://edited-ha:8123';copied.nodes.get('token').value='draft-token';copied.accept(B);
 copied.location.hash='#'+B;copied.listeners.hashchange();await settled(copied);
 assert(copied.eval('paired'));assert.equal(copied.nodes.get('url').value,'http://edited-ha:8123');assert.equal(copied.nodes.get('token').value,'draft-token');assert.equal(copied.requests.at(-1).options.headers['X-Setup-Key'],B);
 // Expiration during saving exposes recovery, preserves credentials, and never retries the save.
 copied.accept(A);const count=copied.requests.length;copied.nodes.get('save').onclick();await settled(copied);
 assert.equal(copied.requests.length,count+1);assert(copied.nodes.get('editor').classList.contains('hidden'));assert.equal(copied.nodes.get('token').value,'draft-token');assert.match(copied.nodes.get('message').textContent,/missing or expired/);
 copied.nodes.get('setupLink').value='http://device:8080/#'+A;copied.nodes.get('pair').onclick();await settled(copied);assert(copied.eval('paired'));assert.equal(copied.nodes.get('token').value,'draft-token');
 const stale=browser('',B);await settled(stale);assert(!stale.eval('paired'));assert(stale.nodes.get('editor').classList.contains('hidden'));
 const reload=browser('',A);await settled(reload);assert(reload.eval('paired'));assert.equal(reload.location.hash,'#'+A);
 const privateMode=browser('#'+A,'',true);await settled(privateMode);assert(privateMode.eval('paired'));assert.equal(privateMode.location.hash,'#'+A);
 console.log('Setup browser: missing/stale keys, cross-browser copy, hash changes, draft recovery and blocked storage passed');
})().catch(e=>{console.error(e);process.exitCode=1});
