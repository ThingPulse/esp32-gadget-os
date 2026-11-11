import {app,BrowserWindow,Tray,Menu,nativeImage,ipcMain,dialog,shell,powerMonitor} from 'electron';
import {join,dirname} from 'node:path';
import {fileURLToPath} from 'node:url';
import {CompanionService} from '../src/desktop-service.mjs';
import {loadState,saveState} from '../src/state.mjs';
import {readTaskHistory} from '../src/task-history.mjs';
import {activityDirectory} from '../src/activity.mjs';
import {configureActivity} from '../src/activity-setup.mjs';
import {collectDesktopCodex} from './codex-path.mjs';

const directory=dirname(fileURLToPath(import.meta.url));
const smoke=process.env.GADGET_COMPANION_SMOKE==='1';
if(smoke){app.setPath('userData',process.env.GADGET_COMPANION_TEST_DIR);app.disableHardwareAcceleration();}
let window,tray,service,quitting=false,preferences={codexEnabled:false,codexPath:''},preferencesPath;
let operation=false;
const labels={setup:'Setup required',searching:'Looking for Gadgets',choose:'Choose a Gadget',pairing:'Confirm pairing',connected:'Connected',offline:'Gadget offline',repair:'Pairing required'};
function status(){return {...service.status(),preferences:{...preferences,startAtLogin:app.isPackaged&&!smoke?app.getLoginItemSettings().openAtLogin:false},packaged:app.isPackaged,version:app.getVersion()};}
function publish(){
  const value=status();if(window&&!window.isDestroyed())window.webContents.send('companion:status',value);
  tray?.setToolTip(`Gadget Companion — ${labels[value.phase]||'Setup required'}`);
  tray?.setContextMenu(Menu.buildFromTemplate([
    {label:labels[value.phase]||'Setup required',enabled:false},
    ...(value.device?[{label:value.device.name,enabled:false}]:[]),{type:'separator'},
    {label:'Open Gadget Companion…',click:show},
    {label:'Reconnect',enabled:!!value.device,click:()=>service.reconnect()},
    {type:'separator'},{label:'Quit Gadget Companion',click:()=>app.quit()},
  ]));
}
function show(){if(window){window.show();window.focus();}}
async function savePreferences(){await saveState(preferencesPath,preferences);publish();}
async function action(name,value){
  switch(name){
    case 'status':return status();
    case 'history':return readTaskHistory(smoke?join(app.getPath('userData'),'activity'):activityDirectory(),{offset:Number.isSafeInteger(value)&&value>=0?value:0,limit:20});
    case 'search':return service.search(typeof value==='string'?value:undefined);
    case 'pair':if(typeof value!=='string')throw new Error('Choose your Gadget.');return service.pair(value);
    case 'cancel':return service.cancelPairing();
    case 'reconnect':return service.reconnect();
    case 'forget':{
      if(smoke)return service.forget();
      const result=await dialog.showMessageBox(window,{type:'question',message:'Forget this Gadget?',detail:'This computer will stop sending updates. You can pair it again later.',buttons:['Cancel','Forget Gadget'],defaultId:0,cancelId:0});
      if(result.response===1)await service.forget();return;
    }
    case 'codex':
      if(typeof value!=='boolean')throw new Error('Invalid setting.');
      preferences.codexEnabled=value;service.configureCodex(value,preferences.codexPath||'codex');await savePreferences();
      if(value)void service.checkCodex();return;
    case 'activity':{
      if(!value||!['codex','claude'].includes(value.provider)||typeof value.enabled!=='boolean')throw new Error('Invalid activity setting.');
      if(smoke)throw new Error('Hook setup is disabled in smoke tests.');
      const message=await configureActivity(value.provider,value.enabled);
      preferences[value.provider+'Activity']=value.enabled;await savePreferences();
      return value.enabled?message:'Companion hooks removed. Other hooks are unchanged.';
    }
    case 'check-codex':return service.checkCodex();
    case 'choose-codex':{
      const result=await dialog.showOpenDialog(window,{title:'Choose the Codex CLI executable',properties:['openFile','showHiddenFiles']});
      if(!result.canceled&&result.filePaths[0]){preferences.codexPath=result.filePaths[0];service.configureCodex(preferences.codexEnabled,preferences.codexPath);await savePreferences();void service.checkCodex();}return;
    }
    case 'login':
      if(typeof value!=='boolean'||!app.isPackaged||smoke)throw new Error('Start at login is available after installing Gadget Companion.');
      app.setLoginItemSettings({openAtLogin:value,args:['--background']});publish();return;
    case 'help':return shell.openExternal('https://github.com/ThingPulse/esp32-gadget-os/tree/master/tools/gadget-companion#agent-usage-setup');
    case 'test-close-window':
      if(!smoke)throw new Error('Unknown action.');
      setTimeout(()=>window.close(),50);return;
    case 'quit':app.quit();return;
    default:throw new Error('Unknown action.');
  }
}
if(!app.requestSingleInstanceLock()){app.quit();}
else {
  app.on('second-instance',show);app.on('activate',show);
  app.on('window-all-closed',()=>{});
  app.on('before-quit',()=>{quitting=true;service?.stop();});
  app.whenReady().then(async()=>{
  app.setName('Gadget Companion');app.dock?.hide();
  preferencesPath=join(app.getPath('userData'),'preferences.json');
  try{const saved=await loadState(preferencesPath);if(saved)preferences={codexEnabled:saved.codexEnabled===true,codexPath:typeof saved.codexPath==='string'?saved.codexPath:'',codexActivity:saved.codexActivity===true,claudeActivity:saved.claudeActivity===true};}catch{}
  service=new CompanionService({collect:collectDesktopCodex,...(smoke?{statePath:join(app.getPath('userData'),'pairing.json'),autoPoll:false,transport:{discover:async()=>[{id:'AABBCCDDEEFF',name:'Gadget Desk',address:'192.168.1.42'}],request:async(_address,route)=>({status:route==='/v1/pair'?202:200,body:{system:1}})},pause:()=>new Promise(resolve=>setTimeout(resolve,300)),collect:async()=>{throw new Error('Codex is not installed.');}}:{})});
  service.configureCodex(preferences.codexEnabled,preferences.codexPath||'codex');
  const icon=nativeImage.createFromPath(join(directory,'../assets/trayTemplate.png'));icon.setTemplateImage(true);
  tray=new Tray(icon);tray.on('double-click',show);
  window=new BrowserWindow({width:560,height:760,minWidth:480,minHeight:640,show:false,title:'Gadget Companion',backgroundColor:'#111b29',autoHideMenuBar:true,
    webPreferences:{preload:join(directory,'preload.cjs'),contextIsolation:true,nodeIntegration:false,sandbox:true}});
  window.on('close',event=>{if(!quitting){event.preventDefault();window.hide();}});
  window.webContents.setWindowOpenHandler(()=>({action:'deny'}));
  window.webContents.on('will-navigate',event=>event.preventDefault());
  window.webContents.session.setPermissionRequestHandler((_contents,_permission,callback)=>callback(false));
  ipcMain.handle('companion:action',async(event,name,value)=>{
    if(event.sender!==window.webContents||event.senderFrame!==window.webContents.mainFrame)throw new Error('Unknown sender.');
    // Cancellation is deliberately available while a pairing request is pending.
    if(operation&&!['status','cancel','history'].includes(name))return {error:'Please wait for the current action to finish.'};
    const locked=!['status','cancel','history'].includes(name);if(locked)operation=true;
    try{return {value:await action(name,value)};}catch(error){return {error:error.message};}finally{if(locked)operation=false;}
  });
  service.on('change',publish);await service.initialize();await window.loadFile(join(directory,'index.html'));publish();
  if(!process.argv.includes('--background')||service.status().phase==='setup')show();
  powerMonitor.on('resume',()=>service.reconnect());
  if(preferences.codexEnabled&&!smoke)void service.checkCodex();
  }).catch(error=>{console.error(error);app.exit(1);});
}
