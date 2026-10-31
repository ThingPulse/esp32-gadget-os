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
 let accepted=A, discovery={entities:[],total:0,offset:0,page_size:25,query:"",kind:"",busy:false,error:""}, discoveryError="", holdStatus=null;
 const context=vm.createContext({console, URL, Blob, setTimeout:(fn,ms)=>setTimeout(fn,ms===500?0:ms), clearTimeout, location,
  history:{replaceState(_,__,url){location.hash=url.includes('#')?'#'+url.split('#')[1]:''}},
  sessionStorage:{getItem:k=>{if(blockedStorage)throw Error('Blocked');return storage.get(k)},setItem:(k,v)=>{if(blockedStorage)throw Error('Blocked');storage.set(k,v)},removeItem:k=>storage.delete(k)},
  document:{getElementById:id=>nodes.get(id),createElement:element,querySelectorAll:s=>s==='nav button'||s==='[data-step]'?nav:[]},
  window:{addEventListener:(event,fn)=>listeners[event]=fn},
  fetch:async(path,options)=>{
   requests.push({path,options});const valid=options.headers['X-Setup-Key']===accepted;
   if(valid&&path==='/api/discover'){
    const args=JSON.parse(options.body),all=Array.from({length:61},(_,i)=>({id:'light.room_'+i,name:'Room '+i,available:i!==0}));
    const matches=all.filter(e=>(!args.hide_unavailable||e.available)&&(!args.kind||e.id.startsWith(args.kind+'.'))&&(e.id+' '+e.name).toLowerCase().includes(args.query.toLowerCase()));
    discovery={entities:matches.slice(args.offset,args.offset+25),total:matches.length,offset:args.offset,page_size:25,query:args.query,kind:args.kind,hide_unavailable:args.hide_unavailable,busy:false,error:discoveryError};
   }
   if(path==='/api/status'&&holdStatus)await holdStatus;
   return {ok:valid,status:valid?200:403,json:async()=>valid?(path==='/api/config'&&options.method==='GET'?{url:'http://ha:8123',ca:'',has_token:false,pages:[{name:'Home',tiles:[]}]}:path==='/api/status'?discovery:{ok:true}):{error:'Open setup on the device and use its link'}};
  }
 });
 vm.runInContext(script,context);
 return {nodes,requests,location,storage,context,listeners,accept:key=>accepted=key,searchError:error=>discoveryError=error,holdStatus:promise=>holdStatus=promise,eval:s=>vm.runInContext(s,context)};
}
async function settled(b){for(let i=0;i<100;i++){await new Promise(r=>setTimeout(r,2));if(!b.eval('working'))return}throw Error('Page still busy')}
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
 // Opening the picker and saving a connection must not discover anything.
 const picker=browser('#'+A);await settled(picker);assert.equal(picker.nodes.get('kind').value,'light');
 picker.eval('step(2)');assert.equal(picker.requests.filter(r=>r.path==='/api/discover').length,0);
 picker.nodes.get('connect').onclick();await settled(picker);assert.equal(picker.requests.filter(r=>r.path==='/api/discover').length,0);
 assert(picker.nodes.get('hideUnavailable').checked);
 picker.nodes.get('searchAll').onclick();await settled(picker);
 assert.equal(picker.eval('discoveryTotal'),60);assert(picker.eval('entities.every(e=>e.available)'));
 picker.nodes.get('hideUnavailable').checked=false;picker.nodes.get('hideUnavailable').onchange();
 picker.nodes.get('searchAll').onclick();await settled(picker);
 assert.equal(picker.eval('discoveryTotal'),61);assert(!picker.eval('entities[0].available'));
 const beforeFilterCache=picker.requests.length;
 picker.nodes.get('hideUnavailable').checked=true;picker.nodes.get('hideUnavailable').onchange();
 assert.equal(picker.eval('discoveryTotal'),60);assert(picker.eval('entities.every(e=>e.available)'));
 picker.nodes.get('hideUnavailable').checked=false;picker.nodes.get('hideUnavailable').onchange();assert.equal(picker.requests.length,beforeFilterCache);
 assert.equal(picker.eval('entities.length'),25);assert.equal(picker.nodes.get('entityCount').textContent,'25 of 61 matching entities');
 let first=picker.nodes.get('entities').children[0];assert.equal(first.children[1].textContent,'Add');first.children[1].onclick();
 assert.equal(picker.nodes.get('slots').textContent,'1 of 7 slots used');assert.equal(picker.nodes.get('entities').children[0].children[1].textContent,'Added');assert(picker.nodes.get('entities').children[0].children[1].disabled);
 picker.nodes.get('hideUnavailable').checked=true;picker.nodes.get('hideUnavailable').onchange();
 assert(picker.eval('entities.every(e=>e.available)'));assert.equal(picker.eval('config.pages[0].tiles[0].id'),'light.room_0');
 picker.nodes.get('hideUnavailable').checked=false;picker.nodes.get('hideUnavailable').onchange();
 assert.equal(picker.nodes.get('entities').children[0].children[1].textContent,'Added');
 picker.nodes.get('entitiesNext').onclick();await settled(picker);assert.equal(picker.eval('entities.length'),50);assert.equal(picker.eval('config.pages[0].tiles.length'),1);
 picker.nodes.get('entitiesNext').onclick();await settled(picker);assert.equal(picker.eval('entities.length'),61);assert(picker.nodes.get('entitiesNext').classList.contains('hidden'));assert(picker.nodes.get('entitiesNext').disabled);
 const fetched=picker.requests.length;picker.eval('step(3);step(2)');assert.equal(picker.requests.length,fetched);
 picker.nodes.get('kind').value='switch';picker.nodes.get('kind').onchange();assert.equal(picker.requests.length,fetched);
 picker.nodes.get('kind').value='light';picker.nodes.get('kind').onchange();assert.equal(picker.eval('entities.length'),25);assert.equal(picker.requests.length,fetched);
 picker.nodes.get('entitiesNext').onclick();await settled(picker);assert.equal(picker.eval('entities.length'),50);assert.equal(picker.requests.length,fetched);
 picker.nodes.get('search').value='Room 60';picker.nodes.get('search').oninput();assert.equal(picker.eval('entities.length'),0);assert.equal(picker.requests.length,fetched);
 picker.nodes.get('searchAll').onclick();await settled(picker);assert.equal(picker.eval('entities.length'),1);
 // Refresh bypasses the cache, while search/page navigation retains selected tiles.
 let countSearch=picker.requests.filter(r=>r.path==='/api/discover').length;picker.nodes.get('reload').onclick();await settled(picker);assert.equal(picker.requests.filter(r=>r.path==='/api/discover').length,countSearch+1);
 picker.eval("config.pages[0].tiles=Array.from({length:6},(_,i)=>({id:'light.filled_'+i,name:'Filled',icon:'auto'}));render()");assert(!picker.nodes.get('entities').children[0].children[1].disabled);picker.nodes.get('entities').children[0].children[1].onclick();assert.equal(picker.eval('config.pages[0].tiles.length'),7);assert.equal(picker.nodes.get('slots').textContent,'7 of 7 slots used');assert(picker.nodes.get('entities').children[0].children[1].disabled);assert(picker.nodes.get('addManual').disabled);
 picker.eval("config.pages.push({name:'Second',tiles:[]})");picker.nodes.get('targetPage').onchange({target:{value:'1'}});assert(!picker.nodes.get('entities').children[0].children[1].disabled);
 // Metadata from an older request must not replace newly chosen criteria.
 let release;picker.holdStatus(new Promise(r=>release=r));picker.nodes.get('search').value='Room 5';picker.nodes.get('search').oninput();picker.nodes.get('searchAll').onclick();
 await new Promise(r=>setTimeout(r,10));picker.nodes.get('search').value='no match';picker.nodes.get('search').oninput();release();await settled(picker);assert.equal(picker.eval('entities.length'),0);picker.holdStatus(null);
 picker.nodes.get('searchAll').onclick();await settled(picker);assert.equal(picker.nodes.get('entityCount').textContent,'0 of 0 matching entities');
 // Discovery permission failures leave pairing intact and manual entry usable.
 picker.searchError('Entity search requires a Home Assistant administrator token. You can still add entities by ID.');picker.nodes.get('reload').onclick();await settled(picker);assert(picker.eval('paired'));assert.match(picker.nodes.get('message').textContent,/administrator token/);
 picker.nodes.get('manual').value='switch.kitchen';picker.nodes.get('addManual').onclick();assert.equal(picker.eval('config.pages[1].tiles[0].id'),'switch.kitchen');
 // Cache entries are bounded, and changing the saved connection clears them.
 picker.eval("for(let i=0;i<45;i++)cacheResult({kind:'light',query:String(i)},{offset:0,entities:[],total:0,page_size:25});");assert.equal(picker.eval('discoveryCache.size'),40);
 picker.nodes.get('url').value='http://different-ha:8123';picker.nodes.get('save').onclick();await settled(picker);assert.equal(picker.eval('discoveryCache.size'),0);
 console.log('Entity picker: lazy discovery, 25-result batches, cached navigation, Add/Added, capacity, stale results and permission recovery passed');
 console.log('Setup browser: missing/stale keys, cross-browser copy, hash changes, draft recovery and blocked storage passed');
})().catch(e=>{console.error(e);process.exitCode=1});
