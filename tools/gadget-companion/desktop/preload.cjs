const {contextBridge,ipcRenderer}=require('electron');
contextBridge.exposeInMainWorld('companion',{
  action:(name,value)=>ipcRenderer.invoke('companion:action',name,value),
  subscribe:callback=>{
    const listener=(_event,status)=>callback(status);ipcRenderer.on('companion:status',listener);
    return ()=>ipcRenderer.removeListener('companion:status',listener);
  },
});
