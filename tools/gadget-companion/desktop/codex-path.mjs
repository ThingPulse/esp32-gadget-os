import {access,realpath,readdir} from 'node:fs/promises';
import {constants} from 'node:fs';
import {homedir} from 'node:os';
import {join} from 'node:path';
import {spawn} from 'node:child_process';
import {collectCodex} from '../src/codex.mjs';

export async function findCodex(preferred=''){
  const home=homedir();
  const dirs=[...(process.env.PATH||'').split(':'),'/opt/homebrew/bin','/usr/local/bin',join(home,'.local/bin'),join(home,'.volta/bin')];
  try{for(const version of (await readdir(join(home,'.nvm/versions/node'))).sort().reverse())dirs.push(join(home,'.nvm/versions/node',version,'bin'));}catch{}
  for(const file of preferred?[preferred]:dirs.filter(Boolean).map(dir=>join(dir,'codex'))){
    try{await access(file,constants.X_OK);return await realpath(file);}catch{}
  }
  return preferred||'codex';
}
export async function collectDesktopCodex(options){
  const executable=await findCodex(options.executable==='codex'?'':options.executable);
  return collectCodex({...options,executable,spawnProcess:(file,args,settings)=>{
    // npm's Codex entry uses env node. Use Electron's bundled Node runtime so
    // opening Companion from Finder does not depend on the shell's PATH.
    if(/\.[cm]?js$/.test(file))return spawn(process.execPath,[file,...args],{...settings,env:{...process.env,ELECTRON_RUN_AS_NODE:'1'}});
    return spawn(file,args,settings);
  }});
}
