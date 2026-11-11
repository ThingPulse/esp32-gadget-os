import {spawnSync} from 'node:child_process';
import {mkdtemp,rm,mkdir,symlink} from 'node:fs/promises';
import {tmpdir} from 'node:os';
import {join,resolve} from 'node:path';
if(process.platform!=='darwin')throw new Error('Build the macOS download on a Mac.');
const arch=process.argv.find(a=>a.startsWith('--arch='))?.slice(7)||process.arch;
if(!['arm64','x64'].includes(arch))throw new Error('Choose arm64 or x64.');
function run(file,args){const r=spawnSync(file,args,{stdio:'inherit'});if(r.error)throw r.error;if(r.status!==0)throw new Error(`${file} failed (${r.status}).`);}
run(process.execPath,['node_modules/@electron-forge/cli/dist/electron-forge.js','make','--platform=darwin',`--arch=${arch}`]);
const staging=await mkdtemp(join(tmpdir(),'gadget-companion-dmg-'));
try {
  const bundle=resolve(`out/Gadget Companion-darwin-${arch}/Gadget Companion.app`);
  await mkdir('out/downloads',{recursive:true});
  run('/usr/bin/ditto',[bundle,join(staging,'Gadget Companion.app')]);
  await symlink('/Applications',join(staging,'Applications'));
  run('/usr/bin/hdiutil',['create','-volname','Gadget Companion','-srcfolder',staging,'-ov','-format','UDZO',resolve(`out/downloads/Gadget-Companion-${arch}.dmg`)]);
  if(process.env.GADGET_COMPANION_RELEASE==='1'){
    run('/usr/bin/xcrun',['notarytool','submit',resolve(`out/downloads/Gadget-Companion-${arch}.dmg`),'--key',process.env.APPLE_API_KEY,'--key-id',process.env.APPLE_API_KEY_ID,'--issuer',process.env.APPLE_API_ISSUER,'--wait']);
    run('/usr/bin/xcrun',['stapler','staple',resolve(`out/downloads/Gadget-Companion-${arch}.dmg`)]);
  }
} finally {await rm(staging,{recursive:true,force:true});}
