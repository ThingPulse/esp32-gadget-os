static constexpr char AppsPage[]=R"HTML(<!doctype html><html lang="en"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>Gadget OS · Apps</title><style>
:root{color-scheme:dark;font:16px system-ui;background:#172131;color:#edf4fa}body{max-width:760px;margin:40px auto;padding:0 20px}h1{font-size:32px}h2{font-size:20px}section{background:#213247;border-radius:16px;padding:24px;margin:20px 0}button,input{font:inherit}button{padding:12px 18px;border:0;border-radius:9px;background:#67e6c3;color:#132b2d;cursor:pointer}button.secondary{background:#354d67;color:white}button:disabled{opacity:.45;cursor:default}.row{display:flex;align-items:center;gap:12px;padding:18px 0;border-bottom:1px solid #405368}.info{flex:1}small{display:block;color:#b6c7d8;margin-top:6px}#status{min-height:24px;color:#80ebcf;white-space:pre-wrap}input{max-width:100%;margin:12px 0}progress{width:100%;margin-top:15px}.bad{color:#ffb4a9!important}@media(max-width:480px){.row{flex-wrap:wrap}.info{min-width:100%}}
</style><h1>Apps</h1><p>Install apps without updating Gadget OS.</p><p id="status" role="status" aria-live="polite">Connecting…</p>
<section><h2>App catalog</h2><p id="catalog-status">Connect to your device to browse compatible apps.</p><div id="catalog"></div><button class="secondary" id="catalog-reload">Refresh catalog</button></section>
<section><h2>Install or update from a file</h2><p id="policy"></p><p>Choose a <strong>.gapp</strong> package for this device. Packages are signature-checked by the device. Native apps have full device access.</p><input id="file" type="file" accept=".gapp"><p id="preview">Choose an app package.</p><button id="install" disabled>Install app</button><progress id="progress" hidden max="100"></progress></section>
<section><h2>Installed apps</h2><small id="device"></small><div id="apps"></div><button class="secondary" id="reload">Refresh list</button></section>
<script>
const $=id=>document.getElementById(id);let key=location.hash.slice(1),apps=[],busy=false,expected='',selected=null,device=null,catalog=[];
if(key){sessionStorage.setItem('app-key',key);history.replaceState(null,'',location.pathname)}else key=sessionStorage.getItem('app-key')||'';
function status(text,bad=false){$('status').textContent=text;$('status').classList.toggle('bad',bad)}
function lock(value){busy=value;document.querySelectorAll('button,input').forEach(e=>e.disabled=value||e.dataset.running==='1');$('install').disabled=value||!selected}
async function request(path,method='GET'){const r=await fetch(path,{method,headers:{'X-App-Key':key},cache:'no-store'});const d=await r.json();if(!r.ok)throw Error(d.error||'Request failed');return d}
async function refresh(){const d=await request('/api/apps');device=d;apps=d.apps;$('policy').textContent=d.developerMode?'Developer mode is enabled: unsigned custom apps are allowed.':'Only signed apps are accepted. To use unsigned custom apps, enable Developer mode on the device.';$('device').textContent=`Device: ${d.chip} · ${apps.length} / ${d.maxApps} apps`;$('apps').replaceChildren();
if(!apps.length){const p=document.createElement('p');p.textContent='No apps installed yet.';$('apps').append(p)}
for(const app of apps){const row=document.createElement('div');row.className='row';const info=document.createElement('div');info.className='info';const name=document.createElement('strong');name.textContent=app.name;const detail=document.createElement('small');detail.textContent=`${app.version} · ${app.id}${app.trusted?'':' · Unsigned'}${app.running?' · Running — close it before changing it':''}`;info.append(name,detail);
const update=document.createElement('button');update.textContent='Update';update.className='secondary';update.dataset.running=app.running?'1':'0';update.disabled=app.running||busy;update.onclick=()=>{expected=app.id;selected=null;$('install').disabled=true;$('preview').textContent='Choose a package for '+app.name+'.';$('file').value='';$('file').click()};
const remove=document.createElement('button');remove.textContent='Remove';remove.className='secondary';remove.dataset.running=app.running?'1':'0';remove.disabled=app.running||busy;remove.onclick=async()=>{lock(true);try{await request('/api/apps/'+encodeURIComponent(app.id),'DELETE');await refresh();status(app.name+' removed.')}catch(e){status(e.message,true)}finally{lock(false);await refresh().catch(()=>{})}};
row.append(info,update,remove);$('apps').append(row)}renderCatalog()}
$('file').oncancel=()=>{expected='';selected=null;$('install').disabled=true;$('preview').textContent='Choose an app package.'};
async function selectPackage(f,expectedEntry=null){
selected=null;$('install').disabled=true;
if(f.size>device.maxBytes||f.size<52)throw Error('Invalid package size.');
const bytes=await f.arrayBuffer(),v=new DataView(bytes),magic=new TextDecoder().decode(bytes.slice(0,8));
const offset=magic==='GOSAPP2\0'?124:magic==='GOSAPP1\0'?52:0,n=v.getUint32(8,true);
if(!offset||!n||n>2048||offset+n>bytes.byteLength)throw Error('Invalid package header.');
if(offset===52&&!device.developerMode)throw Error('Unsigned package. Enable Developer mode on the device to install it.');
const m=JSON.parse(new TextDecoder().decode(bytes.slice(offset,offset+n)));
if(m.chip!==device.chip)throw Error('This package is for a different chip.');
if(expected&&expected!==m.id)throw Error('Choose a package for '+expected+'.');
if(expectedEntry&&(m.id!==expectedEntry.id||m.version!==expectedEntry.version||m.chip!==expectedEntry.chip||m.name!==expectedEntry.name))throw Error('Package does not match the catalog.');
expected='';const old=apps.find(a=>a.id===m.id);if(old?.running)throw Error('Close the running app before updating.');
selected=f;$('preview').textContent=`${m.name||m.id} · ${old?old.version+' → ':''}${m.version} · ${m.chip}`;
$('install').textContent=old?'Update app':'Install app';$('install').disabled=busy;status('Package ready. The device will verify its signature before installation.');
}
$('file').onchange=async()=>{try{const f=$('file').files[0];if(f)await selectPackage(f)}catch(e){selected=null;expected='';$('preview').textContent='Choose a valid app package.';status(e.message,true)}};
function catalogURL(value){
const base=new URL(device.catalogUrl),url=new URL(value,base);
if(url.protocol!=='https:'||url.origin!==base.origin||url.username||url.password||url.hash||!url.pathname.startsWith(base.pathname.slice(0,base.pathname.lastIndexOf('/')+1)))throw Error('Unexpected catalog download URL.');
return url.href;
}
async function download(url,limit,expectedSize=0){
const controller=new AbortController(),timeout=setTimeout(()=>controller.abort(),30000);
try{const r=await fetch(catalogURL(url),{credentials:'omit',referrerPolicy:'no-referrer',redirect:'error',cache:'no-cache',signal:controller.signal});
if(!r.ok)throw Error('Catalog download failed ('+r.status+'). You can still install a package from a file.');
const length=Number(r.headers.get('content-length'));if(length>limit)throw Error('Download exceeds size limit.');
const reader=r.body.getReader(),chunks=[];let size=0;
while(true){const {done,value}=await reader.read();if(done)break;size+=value.length;if(size>limit){await reader.cancel();throw Error('Download exceeds size limit.')}chunks.push(value)}
if(expectedSize&&size!==expectedSize)throw Error('Incomplete package download.');return new Blob(chunks);
}finally{clearTimeout(timeout)}
}
function renderCatalog(){
$('catalog').replaceChildren();
for(const item of catalog){
const row=document.createElement('div');row.className='row';
const image=document.createElement('img');image.width=40;image.height=40;image.alt='';image.referrerPolicy='no-referrer';image.src=catalogURL(item.icon);
const info=document.createElement('div');info.className='info';const name=document.createElement('strong');name.textContent=item.name;
const detail=document.createElement('small');detail.textContent=item.version;info.append(name,detail);
const installed=apps.find(a=>a.id===item.id),button=document.createElement('button');
const same=installed?.version===item.version&&installed?.trusted;
button.textContent=same?'Installed':installed?'Update':'Install';button.dataset.running=same||installed?.running?'1':'0';button.disabled=busy||same||!!installed?.running;
button.onclick=async()=>{if(busy)return;lock(true);expected='';try{
status('Downloading '+item.name+'…');const blob=await download(item.url,device.maxBytes,item.bytes);
await selectPackage(blob,item);lock(false);$('install').click();
}catch(e){selected=null;status(e.message,true);lock(false)}};
row.append(image,info,button);$('catalog').append(row);
}}
async function loadCatalog(){
if(!device)return;lock(true);$('catalog-status').textContent='Loading compatible apps…';
try{const blob=await download(device.catalogUrl,65536);const data=JSON.parse(await blob.text());
if(data.schema!==1||!Array.isArray(data.apps)||data.apps.length>128)throw Error('Unsupported app catalog.');
const seen=new Set();catalog=data.apps.filter(a=>a.chip===device.chip&&a.api_major===device.apiMajor&&Number.isInteger(a.api_minor)&&a.api_minor<=device.apiMinor).map(a=>{
if(!/^[a-z][a-z0-9-]{0,31}$/.test(a.id)||typeof a.name!=='string'||a.name.length>32||typeof a.version!=='string'||a.version.length>32||!Number.isInteger(a.bytes)||a.bytes<124||a.bytes>device.maxBytes||seen.has(a.id))throw Error('Invalid catalog entry.');
seen.add(a.id);catalogURL(a.url);catalogURL(a.icon);return a});
renderCatalog();$('catalog-status').textContent=catalog.length?'Apps compatible with your device.':'No compatible apps published yet.';
}catch(e){catalog=[];renderCatalog();$('catalog-status').textContent='Catalog unavailable. '+e.message}
finally{lock(false)}
}
$('catalog-reload').onclick=loadCatalog;
$('install').onclick=()=>{if(!selected||busy)return;lock(true);$('progress').hidden=false;$('progress').value=0;status('Uploading and verifying…');const xhr=new XMLHttpRequest();xhr.open('POST','/api/install');xhr.setRequestHeader('X-App-Key',key);xhr.setRequestHeader('Content-Type','application/octet-stream');xhr.timeout=35000;xhr.upload.onprogress=e=>{if(e.lengthComputable)$('progress').value=e.loaded/e.total*100};
let finished=false;async function finish(message,bad){if(finished)return;finished=true;status(message,bad);$('progress').hidden=true;selected=null;$('file').value='';$('preview').textContent='Choose an app package.';lock(false);try{await refresh()}catch(e){status(e.message,true)}}
xhr.onload=()=>{let d={};try{d=JSON.parse(xhr.responseText)}catch{}finish(xhr.status===200?'Installed. Return to the launcher to open your app.':d.error||'Installation failed.',xhr.status!==200)};xhr.onerror=()=>finish('Connection lost. Refresh the list to check the installation result.',true);xhr.ontimeout=()=>finish('Upload timed out. Refresh the list before retrying.',true);xhr.send(selected)};
$('reload').onclick=()=>refresh().then(()=>status('List updated.')).catch(e=>status(e.message,true));
if(!key)status('Open Apps on the device and scan its QR code to pair this browser.',true);else refresh().then(()=>{status('Paired. Ready to install apps.');return loadCatalog()}).catch(e=>status(e.message,true));
</script></html>)HTML";
