import test from 'node:test';import assert from 'node:assert/strict';
import {memory,cpuTime,processes,network,battery,disk,cpuDelta,SystemCollector} from '../src/system.mjs';
test('macOS parsers exclude file cache and process arguments',()=>{
 assert.equal(memory('page size of 16384 bytes\nAnonymous pages: 100.\nPages wired down: 20.\nPages occupied by compressor: 10.\nPages purgeable: 5.\nFile-backed pages: 999.',1e9),125*16384);
 assert.equal(memory('',1e9),null);assert.equal(cpuTime('2:09.08'),129.08);assert.equal(cpuTime('1-02:03:04'),93784);assert(Number.isNaN(cpuTime('bad')));
 assert.deepEqual(processes('123 2:09.08 1024 /a/Example App'),[{pid:123,seconds:129.08,rss:1048576,name:'Example App'}]);
 const net='en0 1500 <Link#1> aa:bb:cc:dd:ee:ff 10 0 12345 20 0 34567 0';assert.deepEqual(network(net,'en0'),{iface:'en0',rx:12345,tx:34567});assert.equal(network(net,'utun0'),null);
 assert.deepEqual(battery("Now drawing from 'AC Power'\n 82%; charging;"),{battery:82,power:1});assert.equal(battery("Now drawing from 'AC Power'\n100%; charged;").power,2);assert.equal(battery("Now drawing from 'Battery Power'\n82%; discharging;").power,0);assert.equal(battery("Now drawing from 'AC Power'").power,3);assert.equal(battery('').power,-1);
 assert.deepEqual(disk('Filesystem blocks used available\n/dev/disk 1024000 500000 409600 50% /'),{disk_total_mib:1000,disk_free_mib:400});
});
test('interval CPU, process sorting, gaps, cache and payload bounds',async()=>{
 let now=0,round=0,slow=0;const cpu=()=>[{times:{user:round*500,sys:0,idle:round*1500,irq:0,nice:0}}];
 const system={platform:()=> 'darwin',cpus:cpu,totalmem:()=>16*1048576,hostname:()=> 'Example',uptime:()=>100};
 const command=async(file)=>{if(file.endsWith('vm_stat'))return 'page size of 4096 bytes\nAnonymous pages: 1000.\nPages wired down: 20.\nPages occupied by compressor: 10.\nPages purgeable: 5.';if(file.endsWith('/ps'))return `10 0:0${round}.00 1000 /x/Worker\n11 0:00.00 2000 /x/Larger`;if(file.endsWith('pmset'))slow++;return '';};
 const collector=new SystemCollector({command,system,clock:()=>now});let d=await collector.sample();assert.equal(d.cpu,-1);assert.equal(d.mt0_name,'Larger');assert.equal(d.ct0_name,undefined);
 now=2000;round=1;d=await collector.sample();assert.equal(d.cpu,25);assert.equal(d.ct0_name,'Worker');assert.equal(d.ct0_value,500);assert.equal(slow,1);assert.equal(d.cpu_hist,'ff19');assert.equal(d.cpu_cores,'19');
 now=22000;round=2;d=await collector.sample();assert.equal(d.cpu,-1);assert.equal(d.cpu_cores,'ff');assert.equal(d.cpu_hist.length,24);assert(d.cpu_hist.includes('ffffff'));assert(Buffer.byteLength(JSON.stringify(d))<2048);
 await collector.sample(true);assert.equal(slow,2);assert.equal(cpuDelta(null,cpu()),-1);
});
test('full histories and long names fit a device snapshot',async()=>{
 let round=0;
 const system={platform:()=> 'darwin',cpus:()=>Array.from({length:64},()=>({times:{user:round*1000,sys:0,idle:round*1000,irq:0,nice:0}})),totalmem:()=>1024**4,hostname:()=> 'Host'.repeat(40),uptime:()=>2147483647};
 const command=async(file)=>{
  if(file.endsWith('vm_stat'))return 'page size of 4096 bytes\nAnonymous pages: 100000000.\nPages wired down: 2000000.\nPages occupied by compressor: 1000000.\nPages purgeable: 0.';
  if(file.endsWith('/ps'))return Array.from({length:10},(_,i)=>`${99990+i} ${Math.floor(round/60)}:${round%60}.00 ${100000000+i} /x/${'VeryLongProcessName'.repeat(10)}`).join('\n');
  if(file.endsWith('diskutil'))return 'Volume Name: '+ 'Volume'.repeat(30);
  return '';
 };
 const collector=new SystemCollector({command,system,clock:()=>round*2000});let d;
 for(round=0;round<65;round++)d=await collector.sample();
 assert.equal(d.cpu_cores.length,128);assert.equal(d.cpu_hist.length,120);assert.equal(d.ram_hist.length,120);
 assert.equal(d.ct4_name.length,32);assert.equal(d.mt4_name.length,32);
 assert(Buffer.byteLength(JSON.stringify(d))<=2048);
});
test('sub-percent memory changes survive collection and history encoding',async()=>{
 let now=0,pages=842;
 const system={platform:()=> 'darwin',cpus:()=>[{times:{user:0,sys:0,idle:now,irq:0,nice:0}}],totalmem:()=>1000*4096,hostname:()=> 'Example',uptime:()=>100};
 const command=async file=>file.endsWith('vm_stat')?`page size of 4096 bytes\nAnonymous pages: ${pages}.\nPages wired down: 0.\nPages occupied by compressor: 0.\nPages purgeable: 0.`:'';
 const collector=new SystemCollector({command,system,clock:()=>now});
 let d=await collector.sample();assert.equal(d.ram,84);assert.equal(d.ram10,842);
 now=2000;pages=844;d=await collector.sample();assert.equal(d.ram,84);assert.equal(d.ram10,844);
 now=4000;pages=837;d=await collector.sample();assert.equal(d.ram,84);assert.equal(d.ram10,837);
 assert.equal(d.ram_hist,'545453');assert.equal(d.ram_frac,'247');
 now=8000;d=await collector.sample();assert.equal(d.ram_frac,'247f7');assert.equal(d.ram_hist,'545453ff53');
});

test('individual cores retain distinct loads and counter resets become unavailable',()=>{
 const core=(user,idle)=>({times:{user,idle,nice:0,sys:0,irq:0}});
 assert.equal(cpuDelta([core(0,0)],[core(100,0)]),100);
 assert.equal(cpuDelta([core(0,0)],[core(0,100)]),0);
 assert.equal(cpuDelta([core(100,0)],[core(0,100)]),-1);
});
test('collector sends each logical core independently',async()=>{
 let now=0;
 const system={platform:()=> 'darwin',cpus:()=>[0,1].map(i=>({times:{user:i?0:now,idle:i?now:0,nice:0,sys:0,irq:0}})),totalmem:()=>1024,hostname:()=> 'Test',uptime:()=>0};
 const collector=new SystemCollector({system,command:async()=>'',clock:()=>now});
 assert.equal((await collector.sample()).cpu_cores,'ffff');
 now=2000;const sample=await collector.sample();assert.equal(sample.cpu,50);assert.equal(sample.cpu_cores,'6400');
});
