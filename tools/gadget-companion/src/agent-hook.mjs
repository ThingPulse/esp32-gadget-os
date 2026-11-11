// Invoked by agent lifecycle hooks using Node or Electron's bundled Node runtime.
// Always observational: no approval decisions and no hook failures propagated.
import {recordEvent} from './activity.mjs';
let data='';
const timeout=setTimeout(()=>process.exit(0),2000);
try {
 for await(const chunk of process.stdin){data+=chunk;if(data.length>1024*1024)break;}
 if(data.length<=1024*1024)await recordEvent(process.argv.at(-1),JSON.parse(data));
}catch{}
clearTimeout(timeout);
process.stdout.write('{}\n');
