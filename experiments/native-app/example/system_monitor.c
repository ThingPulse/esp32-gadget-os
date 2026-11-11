#include <stddef.h>
#ifndef GADGET_APP_H
#include "gadget_app.h"
#endif
#include <stddef.h>
#ifdef __XTENSA__
void *memcpy(void *d,const void *s,size_t n){unsigned char *a=d;const unsigned char*b=s;while(n--)*a++=*b++;return d;}
void *memset(void *d,int v,size_t n){unsigned char*a=d;while(n--)*a++=v;return d;}
#endif
static const gadget_host_api *host;static const gadget_ui_api *ui;
static gadget_handle panels[3],nav[3],sorts[2],host_name,connection,status,metric[2],sub[2],graphs[2],rates[2],battery_icon,power_text,power_state,uptime_text,proc_names[5],proc_values[5],column,proc_caption,disk_name,disk_free,disk_used,disk_bar,disk_percent;
static unsigned page,sort,elapsed,received,paired,refresh_wait;
static int32_t age,cpu,ram,cores,mem,total,down,up,battery,power,uptime,disk_total,disk_avail;
static int32_t chart_data[2][60],chart_count[2],proc_val[2][5];
static char names[2][5][49],computer[49],volume[49];
typedef struct {char s[160];unsigned n;} Text;
static void add(Text*t,const char*s){while(*s&&t->n<159)t->s[t->n++]=*s++;t->s[t->n]=0;}
static void num(Text*t,unsigned v){char b[12];unsigned n=0;do{b[n++]=(char)('0'+v%10);v/=10;}while(v);while(n){char c[2]={b[--n],0};add(t,c);}}
static void decimal(Text*t,unsigned v){num(t,v/10);add(t,".");num(t,v%10);}
static void memory_text(Text*t,int32_t v){if(v<0){add(t,"--");return;}if(v>=1048576){decimal(t,(unsigned)v/1048576*10+((unsigned)v%1048576*10+524288)/1048576);add(t," TB");}else if(v<1024){num(t,v);add(t," MB");}else{if(v%1024)decimal(t,(unsigned)v/1024*10+((unsigned)v%1024*10+512)/1024);else num(t,(unsigned)v/1024);add(t," GB");}}
static void rate(Text*t,int32_t v){if(v<0){add(t,"--");return;}if(v>=1000000){decimal(t,(unsigned)v/100000);add(t," MB/s");}else if(v>=1000){num(t,(unsigned)v/1000);add(t," KB/s");}else{num(t,v);add(t," B/s");}}
static int32_t number(const char*j,unsigned n,const char*k){int32_t v=-1;if(host->json_number_scaled(j,n,k,1,&v))return -1;return v;}
static int32_t bounded(int32_t v,int32_t maximum){return v>=0&&v<=maximum?v:-1;}
static unsigned hex(char c){return c>='0'&&c<='9'?c-'0':c>='a'&&c<='f'?c-'a'+10:255;}
static void string(const char*j,unsigned n,const char*k,char*v,unsigned cap){if(host->json_string(j,n,k,v,cap)<0)v[0]=0;for(unsigned i=0;v[i];++i)if((unsigned char)v[i]<32||(unsigned char)v[i]>126){v[0]=0;break;}}
static void render(void){
 unsigned offline=received&&((unsigned)age+elapsed>=10);for(unsigned i=0;i<3;++i){ui->visible(panels[i],page==i);ui->selected(nav[i],page==i);ui->opacity(panels[i],offline?45:100);}for(unsigned i=0;i<2;++i)ui->selected(sorts[i],sort==i);
 ui->text(host_name,computer[0]?computer:"Computer");ui->text(connection,offline?"Offline":received?"Live":"Waiting");
 Text footer={{0},0};if(!paired)add(&footer,"Pair in Add apps > Pair computer");else if(!received)add(&footer,"Waiting for computer");else{add(&footer,offline?"Last seen ":"Updated ");num(&footer,(unsigned)age+elapsed);add(&footer,"s ago");}ui->text(status,footer.s);
 for(unsigned i=0;i<2;++i){Text v={{0},0},s={{0},0};int32_t val=i?ram:cpu;if(val<0)add(&v,"--");else{num(&v,val);add(&v,"%");}ui->text(metric[i],v.s);
  if(i){if(mem<0||total<=0)add(&s,"Unavailable");else{unsigned base=total>=1048576?1048576:total>=1024?1024:1;if((unsigned)mem/base>=100)num(&s,((unsigned)mem+base/2)/base);else decimal(&s,(unsigned)mem/base*10+((unsigned)mem%base*10+base/2)/base);add(&s," / ");num(&s,((unsigned)total+base/2)/base);add(&s,base==1048576?" TB":base==1024?" GB":" MB");}}else{if(cores<1)add(&s,"Unavailable");else{num(&s,cores);add(&s,cores>=100?" cores":" logical cores");}}ui->text(sub[i],s.s);
 }
 Text rx={{0},0},tx={{0},0};add(&rx,"\xef\x81\xb8 ");add(&tx,"\xef\x81\xb7 ");rate(&rx,down);rate(&tx,up);ui->text(rates[0],rx.s);ui->text(rates[1],tx.s);
 const char *icons[]={"\xef\x89\x84","\xef\x89\x83","\xef\x89\x82","\xef\x89\x81","\xef\x89\x80"};ui->text(battery_icon,power==3?"\xef\x83\xa7":icons[battery<0?0:battery>=90?4:battery>=65?3:battery>=35?2:battery>=10?1:0]);
 Text b={{0},0};if(power==3)add(&b,"AC power");else if(battery<0)add(&b,"Battery --");else{add(&b,"Battery ");num(&b,battery);add(&b,"%");}ui->text(power_text,b.s);ui->text(power_state,power==3?"No battery":power==2?"Plugged in":power==1?"Charging":power==0?"On battery":"Unavailable");
 Text u={{0},0};if(uptime<0)add(&u,"--");else{num(&u,(unsigned)uptime/86400);add(&u,"d ");num(&u,(unsigned)uptime%86400/3600);add(&u,"h");}ui->text(uptime_text,u.s);
 ui->text(column,sort?"Memory":"CPU");ui->text(proc_caption,sort?"Resident memory per process":"CPU: share of whole system");
 for(unsigned i=0;i<5;++i){ui->text(proc_names[i],names[sort][i][0]?names[sort][i]:i==0?"Unavailable":"");Text t={{0},0};if(names[sort][i][0]){int32_t v=proc_val[sort][i];if(sort)memory_text(&t,v);else if(v<0)add(&t,"--");else{decimal(&t,v);add(&t,"%");}}ui->text(proc_values[i],t.s);}
 ui->text(disk_name,volume[0]?volume:"Startup volume");Text f={{0},0},d={{0},0},p={{0},0};memory_text(&f,disk_avail);if(disk_avail>=0)add(&f," free");
 if(disk_total>0&&disk_avail>=0&&disk_avail<=disk_total){memory_text(&d,disk_total-disk_avail);add(&d," / ");memory_text(&d,disk_total);add(&d," used");unsigned used=(unsigned)(disk_total-disk_avail),capacity=(unsigned)disk_total;while(capacity>20000000){used/=2;capacity/=2;}unsigned percentage=(used*100+capacity/2)/capacity;ui->value(disk_bar,percentage);num(&p,percentage);add(&p,"% used");ui->visible(disk_bar,1);}else{add(&d,"Unavailable");ui->visible(disk_bar,0);}
 ui->text(disk_free,f.s);ui->text(disk_used,d.s);ui->text(disk_percent,p.s);
}
static void read_snapshot(void){char json[2049];uint32_t age_ms=0;int32_t n=host->companion_channel_read("system",json,sizeof json,&age_ms);paired=n!=-2;if(n<=0){render();return;}if(n>2048||number(json,n,"schema")!=1)return;
 int32_t new_age=number(json,n,"age");if(new_age<0||new_age>3600)return;received=1;age=new_age+(int32_t)(age_ms/1000);elapsed=0;
 string(json,n,"host",computer,sizeof computer);string(json,n,"volume",volume,sizeof volume);
 cpu=number(json,n,"cpu");ram=number(json,n,"ram");cores=number(json,n,"cores");mem=number(json,n,"mem_mib");total=number(json,n,"total_mib");down=number(json,n,"down_bps");up=number(json,n,"up_bps");battery=number(json,n,"battery");power=number(json,n,"power");uptime=number(json,n,"uptime");disk_total=number(json,n,"disk_total_mib");disk_avail=number(json,n,"disk_free_mib");
 cpu=bounded(cpu,100);ram=bounded(ram,100);cores=bounded(cores,4096);battery=bounded(battery,100);power=bounded(power,3);
 for(unsigned k=0;k<2;++k){char data[121];string(json,n,k?"ram_hist":"cpu_hist",data,sizeof data);unsigned count=0;while(data[count])++count;count=count%2?0:count/2;if(count>60)count=60;chart_count[k]=count;for(unsigned i=0;i<count;++i){unsigned a=hex(data[i*2]),b=hex(data[i*2+1]),v=a*16+b;chart_data[k][i]=(a<16&&b<16&&v<=100)?(int32_t)v:-1;}ui->chart(graphs[k],chart_data[k],count);
  for(unsigned i=0;i<5;++i){Text key={{0},0};add(&key,k?"mt":"ct");num(&key,i);unsigned prefix=key.n;add(&key,"_name");string(json,n,key.s,names[k][i],sizeof names[k][i]);key.n=prefix;key.s[prefix]=0;add(&key,"_value");proc_val[k][i]=bounded(number(json,n,key.s),k?2147483647:1000);}
 }render();
}
static void choose(void*u){page=(unsigned)(uintptr_t)u;render();}
static void sorting(void*u){sort=(unsigned)(uintptr_t)u;render();}
static void refresh(void*u){(void)u;if(!refresh_wait){host->system_refresh();refresh_wait=2;}}
static void tick(void*u){(void)u;if(refresh_wait)--refresh_wait;if(elapsed<2147480000)++elapsed;read_snapshot();}
static void close_app(void*u){(void)u;host->request_close();}
static void shutdown(void){host=0;}
static gadget_handle make(gadget_handle p,unsigned kind,const char*t,int x,int y,int w,int h,unsigned style,gadget_callback cb,void*u){gadget_handle o=ui->create(p,kind,t,cb,u);if(!o)return 0;if(ui->box(o,x,y,w,h)||ui->style(o,style))return 0;return o;}
static gadget_handle label(gadget_handle p,const char*t,int x,int y,int w,int h,unsigned style){return make(p,GADGET_UI_LABEL,t,x,y,w,h,style,0,0);}
__attribute__((visibility("default"))) int gadget_app_entry(int argc,char**argv){
 if(argc!=2||!argv)return -1;host=(const gadget_host_api*)argv[0];gadget_app_lifecycle*app=(gadget_app_lifecycle*)argv[1];
 if(!host||host->struct_size<sizeof(gadget_host_api)||host->major!=1||host->minor<7||!host->companion_channel_read||!host->system_refresh||!host->json_string||!host->ui||!app||app->struct_size<sizeof(*app))return -1;ui=host->ui;if(ui->struct_size<offsetof(gadget_ui_api,reserve)||!ui->chart||!ui->opacity)return -1;
 app->major=1;app->minor=7;app->shutdown=shutdown;page=sort=elapsed=received=refresh_wait=0;paired=1;age=0;computer[0]=volume[0]=0;cpu=ram=cores=mem=total=down=up=battery=power=uptime=disk_total=disk_avail=-1;for(unsigned k=0;k<2;++k)for(unsigned i=0;i<5;++i){names[k][i][0]=0;proc_val[k][i]=-1;}
 gadget_handle root=ui->create(0,GADGET_UI_ROOT,0,0,0);if(!root)return -1;
 gadget_handle header=make(root,GADGET_UI_CONTAINER,0,0,0,320,44,GADGET_STYLE_HEADER,0,0);
 make(header,GADGET_UI_BUTTON,0,0,0,44,44,GADGET_STYLE_BACK,close_app,0);make(header,GADGET_UI_BUTTON,0,276,0,44,44,GADGET_STYLE_REFRESH,refresh,0);gadget_handle title=label(header,"System Monitor",44,8,232,30,GADGET_STYLE_TITLE);ui->align(title,1);
 host_name=label(root,"Computer",14,52,214,22,GADGET_STYLE_DEFAULT);ui->style(host_name,GADGET_STYLE_ELLIPSIS);connection=label(root,"Waiting",234,52,72,22,GADGET_STYLE_ACCENT);ui->align(connection,2);
 for(unsigned i=0;i<3;++i){panels[i]=make(root,GADGET_UI_CONTAINER,0,0,84,320,316,GADGET_STYLE_DEFAULT,0,0);if(!panels[i])return -1;nav[i]=make(root,GADGET_UI_BUTTON,i==0?"Overview":i==1?"Processes":"Storage",(int)i*106+2,432,104,46,GADGET_STYLE_TAB,choose,(void*)(uintptr_t)i);if(!nav[i])return -1;}
 for(unsigned i=0;i<2;++i){gadget_handle c=make(panels[0],GADGET_UI_CONTAINER,0,10+(int)i*156,0,144,154,GADGET_STYLE_CARD,0,0);label(c,i?"Memory":"CPU",10,8,124,22,GADGET_STYLE_DEFAULT);metric[i]=label(c,"--",10,32,124,40,GADGET_STYLE_VALUE);ui->style(metric[i],GADGET_STYLE_WHITE);sub[i]=label(c,"",10,76,124,20,GADGET_STYLE_MUTED);graphs[i]=make(c,GADGET_UI_CHART,0,10,100,124,27,GADGET_STYLE_DEFAULT,0,0);label(c,"Last 2 min",10,131,124,20,GADGET_STYLE_MUTED);}
 gadget_handle net=make(panels[0],GADGET_UI_CONTAINER,0,10,164,300,78,GADGET_STYLE_CARD,0,0);label(net,"Network",10,6,280,20,GADGET_STYLE_DEFAULT);for(unsigned i=0;i<2;++i){rates[i]=label(net,"--",10+(int)i*146,28,138,22,GADGET_STYLE_DEFAULT);label(net,i?"Upload":"Download",10+(int)i*146,52,138,20,GADGET_STYLE_MUTED);}
 gadget_handle batt=make(panels[0],GADGET_UI_CONTAINER,0,10,252,300,64,GADGET_STYLE_CARD,0,0);battery_icon=label(batt,"\xef\x89\x80",10,20,30,30,GADGET_STYLE_TITLE);power_text=label(batt,"",48,8,154,22,GADGET_STYLE_DEFAULT);power_state=label(batt,"",48,34,154,20,GADGET_STYLE_MUTED);label(batt,"Uptime",210,8,78,20,GADGET_STYLE_MUTED);uptime_text=label(batt,"",210,34,78,20,GADGET_STYLE_DEFAULT);
 for(unsigned i=0;i<2;++i)sorts[i]=make(panels[1],GADGET_UI_BUTTON,i?"Memory":"CPU",10+(int)i*150,0,150,40,GADGET_STYLE_TAB,sorting,(void*)(uintptr_t)i);
 label(panels[1],"Process",14,48,180,22,GADGET_STYLE_MUTED);column=label(panels[1],"CPU",220,48,86,22,GADGET_STYLE_MUTED);ui->align(column,2);
 gadget_handle list=make(panels[1],GADGET_UI_CONTAINER,0,10,74,300,220,GADGET_STYLE_CARD,0,0);for(unsigned i=0;i<5;++i){proc_names[i]=label(list,"",10,10+(int)i*44,190,24,GADGET_STYLE_DEFAULT);ui->style(proc_names[i],GADGET_STYLE_ELLIPSIS);proc_values[i]=label(list,"",202,10+(int)i*44,88,24,GADGET_STYLE_DEFAULT);ui->align(proc_values[i],2);}
 proc_caption=label(panels[1],"",14,296,292,20,GADGET_STYLE_MUTED);
 gadget_handle storage=make(panels[2],GADGET_UI_CONTAINER,0,10,0,300,190,GADGET_STYLE_CARD,0,0);disk_name=label(storage,"",12,12,276,24,GADGET_STYLE_DEFAULT);ui->style(disk_name,GADGET_STYLE_ELLIPSIS);disk_free=label(storage,"--",12,46,276,40,GADGET_STYLE_VALUE);ui->style(disk_free,GADGET_STYLE_WHITE);disk_used=label(storage,"",12,94,276,24,GADGET_STYLE_DEFAULT);disk_bar=make(storage,GADGET_UI_BAR,0,12,132,276,14,GADGET_STYLE_DEFAULT,0,0);disk_percent=label(storage,"",12,158,276,20,GADGET_STYLE_MUTED);
 gadget_handle note=make(panels[2],GADGET_UI_CONTAINER,0,10,204,300,96,GADGET_STYLE_CARD,0,0);label(note,"Startup volume",12,16,276,24,GADGET_STYLE_DEFAULT);label(note,"Available space for your files",12,50,276,22,GADGET_STYLE_MUTED);
 status=label(root,"",14,408,292,22,GADGET_STYLE_MUTED);
 if(!status||!host_name||!connection||!graphs[0]||!graphs[1]||!disk_percent||!proc_caption||!host->timer_create(1000,tick,0))return -1;
 read_snapshot();return 0;
}
