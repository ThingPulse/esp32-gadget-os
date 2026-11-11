import {readFile,copyFile,readdir,rm} from 'node:fs/promises';
import {join} from 'node:path';
import {homedir} from 'node:os';
import {fileURLToPath} from 'node:url';
import {codexEvents} from './task-history.mjs';
import {events} from './activity.mjs';
import {saveState} from './state.mjs';
const marker='--gadget-agent-hook';
const quote=s=>"'"+s.replaceAll("'","'\\''")+"'";
export async function configureActivity(provider,enabled,{home=homedir(),executable=process.execPath,electron=!!process.versions.electron,script=fileURLToPath(new URL('./agent-hook.mjs',import.meta.url))}={}){
 if(!['codex','claude'].includes(provider))throw new Error('Unknown agent.');
 if(process.platform==='win32')throw new Error('Automatic hook setup currently supports macOS and Linux.');
 const path=provider==='codex'?join(process.env.CODEX_HOME||join(home,'.codex'),'hooks.json'):join(home,'.claude','settings.json');
 let config={},exists=false;
 try{config=JSON.parse(await readFile(path,'utf8'));exists=true;}catch(e){if(e.code!=='ENOENT')throw new Error('Could not read agent settings. Existing settings were left unchanged.');}
 if(!config||typeof config!=='object'||Array.isArray(config)||config.hooks&&(typeof config.hooks!=='object'||Array.isArray(config.hooks)))throw new Error('Invalid hook settings; left unchanged.');
 config.hooks??={};
 for(const [event,groups] of Object.entries(config.hooks)){
  if(!Array.isArray(groups)||groups.some(g=>!g||!Array.isArray(g.hooks)))throw new Error('Invalid hook settings; left unchanged.');
  config.hooks[event]=groups.map(g=>({...g,hooks:g.hooks.filter(h=>!h.command?.includes(marker))})).filter(g=>g.hooks.length);
 }
 if(enabled){
  const command=(electron?'ELECTRON_RUN_AS_NODE=1 ':'')+quote(executable)+' '+quote(script)+' '+marker+' '+provider;
  for(const event of [...events,...(provider==='codex'?codexEvents:[])])(config.hooks[event]??=[]).push({hooks:[{type:'command',command,timeout:3}]});
 }
 if(exists)await copyFile(path,path+'.gadget-backup-'+Date.now());
 await saveState(path,config);
 const directory=process.env.GADGET_COMPANION_ACTIVITY_DIR||join(home,'.gadget-companion','activity');
 let providers={};try{providers=JSON.parse(await readFile(join(directory,'.providers.json'),'utf8'));}catch{}
 providers={codex:providers?.codex===true,claude:providers?.claude===true,[provider]:enabled};await saveState(join(directory,'.providers.json'),providers);
 if(!enabled){for(const file of await readdir(directory).catch(()=>[])){if(!/^[a-f0-9]{24}\.json$/.test(file))continue;const target=join(directory,file);try{const s=JSON.parse(await readFile(target,'utf8'));if(s.provider===(provider==='codex'?0:1))await rm(target,{force:true});}catch{}}}
 return provider==='codex'?'Open /hooks in Codex (or Settings > Hooks), review and trust Gadget Companion hooks. Start a new turn.':'Restart Claude Code or review /hooks, then start a new turn.';
}
