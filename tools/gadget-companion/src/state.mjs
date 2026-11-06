import {homedir} from 'node:os';
import {join, dirname} from 'node:path';
import {mkdir,readFile,writeFile,rename,chmod,rm,stat} from 'node:fs/promises';
import {randomBytes} from 'node:crypto';
export const defaultState = () => join(homedir(),'.gadget-companion','pairing.json');
export async function loadState(path) {
  try { if((await stat(path)).size>8192)throw new Error('Pairing file is too large.');return JSON.parse(await readFile(path,'utf8')); }
  catch(e){if(e.code==='ENOENT')return null;throw new Error('Cannot read pairing file. Use --pair to pair again.');}
}
export async function saveState(path,data){
  await mkdir(dirname(path),{recursive:true,mode:0o700});
  const temp=path+'.'+randomBytes(6).toString('hex')+'.tmp';
  try{await writeFile(temp,JSON.stringify(data)+'\n',{mode:0o600,flag:'wx'});await rename(temp,path);if(process.platform!=='win32')await chmod(path,0o600);}
  finally{await rm(temp,{force:true});}
}
