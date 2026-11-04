#!/usr/bin/env node
// Exercise the shipped page's state machine with bounded network/DOM fixtures.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import vm from 'node:vm';
const html=fs.readFileSync('src/experimental/NativeStorePage.inc','utf8');
const script=html.split('<script>')[1].split('</script>')[0];
const nodes=[];
class Element {
  constructor(tag='div'){this.tag=tag;this.children=[];this.dataset={};this.classList={toggle(){}};this.disabled=false;nodes.push(this)}
  append(...items){this.children.push(...items)}
  replaceChildren(...items){this.children=items}
  click(){if(!this.disabled)return this.onclick?.()}
}
const ids=Object.fromEntries([...html.matchAll(/id="([^"]+)"/g)].map(m=>[m[1],new Element(['install','reload','catalog-reload'].includes(m[1])?'button':m[1]==='file'?'input':'div')]));
const catalogUrl='https://example.test/apps/catalog.json',packageUrl='https://example.test/apps/packages/iss.gapp';
const entry={id:'native-iss',name:'ISS Position',version:'0.2.0',chip:'esp32s3',api_major:1,api_minor:1,url:packageUrl,icon:'https://example.test/apps/icon.png'};
function pack(signed=true){let metadata=Buffer.from(JSON.stringify({...entry,executable:'app.elf'}));metadata=Buffer.concat([metadata,Buffer.alloc((-metadata.length)&3,32)]);const head=Buffer.alloc(signed?124:52);head.write(signed?'GOSAPP2\0':'GOSAPP1\0');head.writeUInt32LE(metadata.length,8);return new Blob([head,metadata,Buffer.alloc(3204)])}
const signed=pack();entry.bytes=signed.size;
let installed=[],uploads=0,catalogFailure=false,oversize=false,externalCalls=0;
const device=()=>({chip:'esp32s3',maxApps:8,maxBytes:70908,developerMode:false,catalogUrl,apiMajor:1,apiMinor:1,apps:installed});
const context=vm.createContext({console,URL,Blob,DataView,TextDecoder,AbortController,setTimeout,clearTimeout,
  document:{getElementById:id=>ids[id],createElement:tag=>new Element(tag),querySelectorAll:()=>nodes.filter(n=>['button','input'].includes(n.tag))},
  location:{hash:'#paired',pathname:'/'},history:{replaceState(){}},sessionStorage:{setItem(){},getItem(){return null}},
  fetch:async(url,options)=>{
    if(url==='/api/apps'){assert.equal(options.headers['X-App-Key'],'paired');return Response.json(device())}
    assert.equal(options.credentials,'omit');assert.equal(options.referrerPolicy,'no-referrer');assert.equal(options.redirect,'error');assert.equal(options.headers,undefined);++externalCalls;
    if(url===catalogUrl){if(catalogFailure)return new Response('unavailable',{status:503});return Response.json({schema:1,apps:[entry,{...entry,chip:'esp32'},{...entry,id:'future',api_minor:2}]})}
    assert.equal(url,packageUrl);return new Response(oversize?new Blob([Buffer.alloc(70909)]):signed);
  },
  XMLHttpRequest:class{
    constructor(){this.upload={};this.headers={}}
    open(method,path){assert.equal(method,'POST');assert.equal(path,'/api/install')}
    setRequestHeader(k,v){this.headers[k]=v}
    async send(blob){assert.equal(this.headers['X-App-Key'],'paired');assert.equal(blob.size,signed.size);++uploads;installed=[{...entry,trusted:true,running:false}];this.status=200;this.responseText='{"ok":true}';await this.onload()}
  }
});
await vm.runInContext(script,context);
assert.equal(ids.catalog.children.length,1,'wrong-chip and unsupported API entries filtered');
await ids.catalog.children[0].children[2].click();
for(let i=0;i<10;++i)await new Promise(setImmediate);
assert.equal(uploads,1);assert.equal(ids.catalog.children[0].children[2].textContent,'Installed');assert.equal(ids.catalog.children[0].children[2].disabled,true);
context.unsigned=pack(false);await assert.rejects(vm.runInContext('selectPackage(unsigned)',context),/Developer mode/);
assert.throws(()=>vm.runInContext("catalogURL('https://evil.test/app.gapp')",context),/Unexpected/);
assert.throws(()=>vm.runInContext("catalogURL('https://example.test/other/app.gapp')",context),/Unexpected/);
oversize=true;await assert.rejects(vm.runInContext('download("'+packageUrl+'",70908)',context),/size limit/);
catalogFailure=true;await vm.runInContext('loadCatalog()',context);assert.match(ids['catalog-status'].textContent,/Catalog unavailable/);assert.equal(ids.file.disabled,false);assert.ok(externalCalls>=4);
console.log('PASS: catalog compatibility, install flow, installed state, unsigned policy, URL/size bounds, credentials isolation and offline fallback');
