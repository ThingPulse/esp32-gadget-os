import dgram from 'node:dgram';
import http from 'node:http';
import {networkInterfaces} from 'node:os';
import {isIPv4} from 'node:net';
import {randomBytes, createHash} from 'node:crypto';

export const pairingCode = token => String(createHash('sha256').update(token).digest().readUInt32BE(0) % 1000000).padStart(6, '0');
export const newToken = () => randomBytes(32).toString('hex');
export function localAddress(ip) {
  if (!isIPv4(ip)) return false;
  const [a,b] = ip.split('.').map(Number);
  return a === 10 || (a === 172 && b >= 16 && b <= 31) || (a === 192 && b === 168) || (a === 169 && b === 254);
}
export function broadcastAddresses(interfaces = networkInterfaces()) {
  const result = new Set(['255.255.255.255']);
  for (const entries of Object.values(interfaces)) for (const entry of entries ?? []) {
    if (entry.internal || entry.family !== 'IPv4' || !localAddress(entry.address)) continue;
    const address = entry.address.split('.').map(Number), mask = entry.netmask.split('.').map(Number);
    result.add(address.map((v,i) => (v | (~mask[i] & 255))).join('.'));
  }
  return [...result];
}
export async function discover({timeout=2500, target, signal}={}) {
  if (target && !localAddress(target)) throw new Error('--device must be a private LAN IPv4 address.');
  const nonce = randomBytes(16).toString('hex'), found = new Map(), socket = dgram.createSocket('udp4');
  return new Promise((resolve,reject) => {
    let timer, settled=false;
    const abort=()=>finish(new Error('Cancelled.'));
    const finish = error => { if(settled)return;settled=true;clearTimeout(timer);signal?.removeEventListener('abort',abort);try{socket.close();}catch{} error ? reject(error) : resolve([...found.values()]); };
    if(signal?.aborted){abort();return;}
    signal?.addEventListener('abort',abort,{once:true});
    socket.once('error', finish);
    socket.on('message', (message, remote) => {
      if (message.length > 512 || !localAddress(remote.address) || (target && remote.address !== target)) return;
      try {
        const d = JSON.parse(message);
        if (d.protocol !== 1 || d.nonce !== nonce || d.port !== 8082 || !/^[A-Fa-f0-9]{12}$/.test(d.id) || typeof d.name !== 'string') return;
        found.set(d.id.toUpperCase(), {id:d.id.toUpperCase(), address:remote.address, name:d.name.replace(/[^\x20-\x7e]/g,'').slice(0,48)});
      } catch {}
    });
    socket.bind(0, () => {
      try{socket.setBroadcast(true);}catch(e){finish(e);return;}
      const packet = Buffer.from(`GADGET_COMPANION_DISCOVER_1 ${nonce}`);
      for (const address of target ? [target] : broadcastAddresses()) socket.send(packet,8083,address,()=>{});
      timer = setTimeout(() => finish(), timeout);
    });
  });
}
export function request(address, path, {token, data, timeout=6000, signal}={}) {
  if (!localAddress(address)) return Promise.reject(new Error('Refusing a non-LAN device address.'));
  const body = data === undefined ? undefined : JSON.stringify(data);
  if (body && Buffer.byteLength(body) > 2048) return Promise.reject(new Error('Summary exceeds device limit.'));
  return new Promise((resolve,reject) => {
    // Direct connection: never use proxies, redirects or browser cookies.
    const req = http.request({hostname:address,port:8082,path,signal,method:body===undefined?'GET':'POST',
      headers:{...(token?{'X-Companion-Key':token}:{}),...(body?{'Content-Type':'application/json','Content-Length':Buffer.byteLength(body)}:{})}}, res => {
      let text='';
      res.on('data', chunk => {text+=chunk;if(text.length>4096)req.destroy(new Error('Oversized device response.'));});
      res.on('error', reject);
      res.on('end', () => {try {resolve({status:res.statusCode, body:JSON.parse(text)});}catch {reject(new Error('Invalid device response.'));}});
    });
    const timer=setTimeout(()=>req.destroy(new Error('Device did not respond.')),timeout);
    req.once('close',()=>clearTimeout(timer));req.on('error',reject);req.end(body);
  });
}
// Payload schemas belong to apps. Capability negotiation keeps old firmware usable.
export function snapshotRoute(capabilities,app,channel,legacy){
  const valid=value=>typeof value==='string'&&/^[a-z][a-z0-9-]{0,31}$/.test(value);
  if(!valid(app)||!valid(channel))throw new Error('Invalid companion app/channel');
  if(capabilities?.appChannels===1)return `/v1/apps/${app}/${channel}`;
  if(legacy)return legacy;
  throw new Error('Update Gadget OS to use app data channels.');
}
