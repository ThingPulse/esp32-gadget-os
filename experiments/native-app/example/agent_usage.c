/* API 1.3 app. Same source runs in SDL and in the Xtensa package. */
#ifndef GADGET_APP_H
#include "gadget_app.h"
#endif
#include <stddef.h>
#ifdef __XTENSA__
#include <stddef.h>
void *memcpy(void *dst,const void *src,size_t n){unsigned char *d=dst;const unsigned char *s=src;while(n--)*d++=*s++;return dst;}
void *memset(void *dst,int v,size_t n){unsigned char *d=dst;while(n--)*d++=(unsigned char)v;return dst;}
#endif
#ifndef AGENT_USAGE_URL
#define AGENT_USAGE_URL ""
#endif
#ifndef AGENT_USAGE_DEMO
#define AGENT_USAGE_DEMO 0
#endif
static const gadget_host_api *host;
static const gadget_ui_api *ui;
static gadget_handle heading, status, tabs[3], detail_root, chart_title;
static gadget_handle day_bars[7], day_values[7], day_labels[7], stat_values[4], longest_label;
static unsigned detail_view, period_view;
typedef struct {int32_t v,u;} Count;
static Count totals[2], daily[7], monthly[7], period_totals[3];
static int32_t month_heights[7],period_known[3],period_expected[3];
static int32_t day_dates[7],day_heights[7],streak,best_streak,longest_sec,detail_age;
typedef struct {gadget_handle box, name, freshness, value, primary, secondary, reset, bars[2];} Card;
static Card cards[2];
static char endpoint[256];
static unsigned pending, cooldown, elapsed, received_data, failed;
typedef struct { int32_t used, minutes, reset; } Window;
static Window windows[2][2];
static int32_t enabled[2], ages[2], sample_data;
static const char *names[2]={"Codex","Claude"};
static const char *keys[2][2][3]={
 {{"codex_primary_used","codex_primary_minutes","codex_primary_reset"},{"codex_secondary_used","codex_secondary_minutes","codex_secondary_reset"}},
 {{"claude_primary_used","claude_primary_minutes","claude_primary_reset"},{"claude_secondary_used","claude_secondary_minutes","claude_secondary_reset"}}};
typedef struct {char s[160]; unsigned n;} Text;
static void add(Text *t,const char *s){while(*s && t->n<159)t->s[t->n++]=*s++;t->s[t->n]=0;}
static void num(Text *t,unsigned v){char b[12];unsigned n=0;do{b[n++]=(char)('0'+v%10);v/=10;}while(v);while(n){char s[2]={b[--n],0};add(t,s);}}
static void duration(Text *t,unsigned seconds){if(seconds>=86400){num(t,seconds/86400);add(t,"d ");seconds%=86400;}if(seconds>=3600){num(t,seconds/3600);add(t,"h ");}num(t,(seconds%3600)/60);add(t,"m");}
static void window_name(Text *t,int32_t minutes){
 if(minutes%1440==0){num(t,(unsigned)minutes/1440);add(t," days");}
 else if(minutes%60==0){num(t,(unsigned)minutes/60);add(t," hours");}
 else{num(t,(unsigned)minutes);add(t," min");}
}
static unsigned available(Window *v){return v->used>=0 && v->reset>(int32_t)elapsed;}
static void count_text(Text *t,Count c,unsigned small){
 if(c.v<0 || c.u<0){add(t,"--");return;}
 if(c.u==0){num(t,(unsigned)c.v);return;}
 num(t,(unsigned)c.v/10);
 if(c.v%10 && (!small||c.v<1000)){add(t,".");num(t,(unsigned)c.v%10);}
 const char *units[]={"","K","M","B","T"};add(t,units[c.u]);
}
static void render_detail(void){
 Text title={{0},0};Count total=period_totals[period_view];
 if(detail_age<0)add(&title,"Waiting for companion details");
 else if(total.v<0 || total.u<0)add(&title,"Not reported yet (UTC)");
 else{if(period_known[period_view]<period_expected[period_view])add(&title,"Known: ");count_text(&title,total,0);add(&title," tokens");if(period_known[period_view]<period_expected[period_view]){add(&title," (");num(&title,(unsigned)period_known[period_view]);add(&title,"/");num(&title,(unsigned)period_expected[period_view]);add(&title," days)");}else add(&title," / UTC");}
 ui->text(chart_title,title.s);
 for(unsigned i=0;i<7;++i){
  unsigned index=period_view==0?6:i;Count c=period_view==2?monthly[i]:daily[index];int32_t height=period_view==2?month_heights[i]:day_heights[index];
  unsigned show=period_view==0?i==3:period_view==2?(period_expected[2]>0 && (int32_t)(i*5)<period_expected[2]):1;
  Text value={{0},0},date={{0},0};count_text(&value,c,1);
  if(period_view==0)add(&date,"Today");
  else if(period_view==2){num(&date,i*5+1);unsigned end=i*5+5;if(end>(unsigned)period_expected[2])end=(unsigned)period_expected[2];if(end>i*5+1){add(&date,"-");num(&date,end);}}
  else if(day_dates[i]>=101){unsigned m=(unsigned)day_dates[i]/100,d=(unsigned)day_dates[i]%100;if(m<10)add(&date,"0");num(&date,m);add(&date,"/");if(d<10)add(&date,"0");num(&date,d);}else add(&date,"--");
  ui->box(day_labels[i],period_view==0?120:7+(int)i*41,118,period_view==0?60:40,20);
  ui->text(day_values[i],value.s);ui->text(day_labels[i],date.s);ui->visible(day_values[i],show);ui->visible(day_labels[i],show);
  ui->visible(day_bars[i],show && c.v>=0 && c.u>=0 && height>=0);
  if(height>=0)ui->value(day_bars[i],period_view==0?(c.v>0?100:0):height);
 }
 for(unsigned i=0;i<4;++i){Text value={{0},0};
  if(i<2)count_text(&value,totals[i],0);
  else{int32_t days=i==2?streak:best_streak;if(days<0)add(&value,"--");else{num(&value,(unsigned)days);add(&value,"d");}}
  ui->text(stat_values[i],value.s);
 }
 Text longest={{0},0};add(&longest,"Longest turn: ");if(longest_sec<0)add(&longest,"--");else if(longest_sec<60){num(&longest,(unsigned)longest_sec);add(&longest,"s");}else duration(&longest,(unsigned)longest_sec);
 ui->text(longest_label,longest.s);
}
static void render(void){
 ui->visible(detail_root,detail_view);ui->text(heading,detail_view?"Codex usage":"Remaining");
 for(unsigned i=0;i<3;++i)ui->selected(tabs[i],period_view==i);
 unsigned row=0;
 for(unsigned p=0;p<2;++p){Card *c=&cards[p];ui->visible(c->box,!detail_view && enabled[p]!=0);if(!enabled[p])continue;
  ui->box(c->box,10,56+(int32_t)row++*172,300,160);
  ui->text(c->freshness,(ages[p]>=0 && (unsigned)ages[p]+elapsed>300)?"Stale":"");
  Window *first=&windows[p][0], *second=&windows[p][1];
  Text value={{0},0}, primary={{0},0}, secondary={{0},0}, reset={{0},0};
  if(available(first)){
   num(&value,(unsigned)(100-first->used));add(&value,"%");
   window_name(&primary,first->minutes);add(&primary," left  /  ");duration(&primary,(unsigned)first->reset-elapsed);add(&primary," to reset");
  }else{add(&value,"--");add(&primary,first->used<0?"Allowance unavailable":"Reset reached - awaiting update");}
  if(available(second)){
   window_name(&secondary,second->minutes);add(&secondary,"   ");num(&secondary,(unsigned)(100-second->used));add(&secondary,"% left");
   add(&reset,"Resets in ");duration(&reset,(unsigned)second->reset-elapsed);
  }else{add(&secondary,second->used<0?"No second limit reported":"Reset reached - awaiting update");}
  ui->text(c->value,value.s);ui->text(c->primary,primary.s);ui->text(c->secondary,secondary.s);ui->text(c->reset,reset.s);
  for(unsigned w=0;w<2;++w){Window *v=&windows[p][w];ui->visible(c->bars[w],available(v));if(available(v))ui->value(c->bars[w],100-v->used);}
 }
 if(detail_view)render_detail();
 if(!pending){Text t={{0},0};
  if(AGENT_USAGE_DEMO || sample_data)add(&t,"Sample / ");
  if(detail_view){if(detail_age<0)add(&t,"Update companion / check connection");else{unsigned age=(unsigned)detail_age+elapsed;add(&t,age>300?"Stale / updated ":"Updated ");num(&t,age);add(&t,"s ago");}}
  else if(failed)add(&t,"Refresh failed - tap refresh");
  else if(received_data){unsigned age=elapsed;if(!endpoint[0]&&!AGENT_USAGE_DEMO&&ages[0]>=0)age+=(unsigned)ages[0];add(&t,"Updated ");num(&t,age);add(&t,"s ago");}
  else add(&t,endpoint[0]?"No data yet":"Pair in Add apps > Pair computer");
  ui->text(status,t.s);
 }
}
static int readnum(const char *s,uint32_t n,const char *key,int32_t *v){return host->json_number_scaled(s,n,key,1,v);}
static int32_t optional(const char *json,uint32_t size,const char *key,int32_t maximum){int32_t v=-1;if(readnum(json,size,key,&v)||v<0||v>maximum)return -1;return v;}
static Count read_count(const char *json,uint32_t size,const char *prefix){Text key={{0},0};add(&key,prefix);add(&key,"_v");int32_t v=optional(json,size,key.s,9999);key.n=0;key.s[0]=0;add(&key,prefix);add(&key,"_u");int32_t unit=optional(json,size,key.s,4);return (Count){v,unit};}
static void read_detail(const char *json,uint32_t size){
 detail_age=optional(json,size,"codex_detail_age",604800);
 totals[0]=read_count(json,size,"codex_lifetime");totals[1]=read_count(json,size,"codex_peak");
 streak=optional(json,size,"codex_streak",36500);best_streak=optional(json,size,"codex_best_streak",36500);longest_sec=optional(json,size,"codex_longest_sec",31536000);
 for(unsigned i=0;i<7;++i){Text key={{0},0};add(&key,"codex_d");num(&key,i);daily[i]=read_count(json,size,key.s);unsigned n=key.n;
  add(&key,"_date");day_dates[i]=optional(json,size,key.s,1231);key.n=n;key.s[n]=0;add(&key,"_bar");day_heights[i]=optional(json,size,key.s,100);
 }
 for(unsigned i=0;i<3;++i){Text key={{0},0};add(&key,"cx_p");num(&key,i);period_totals[i]=read_count(json,size,key.s);unsigned n=key.n;add(&key,"_n");period_known[i]=optional(json,size,key.s,31);key.n=n;key.s[n]=0;add(&key,"_e");period_expected[i]=optional(json,size,key.s,31);if(period_known[i]<0 || period_expected[i]<1 || period_known[i]>period_expected[i] || detail_age<0)period_totals[i]=(Count){-1,-1};}
 for(unsigned i=0;i<7;++i){Text key={{0},0};add(&key,"cx_m");num(&key,i);monthly[i]=read_count(json,size,key.s);add(&key,"_bar");month_heights[i]=optional(json,size,key.s,100);if(detail_age<0)monthly[i]=(Count){-1,-1};}
 if(detail_age<0){totals[0]=totals[1]=(Count){-1,-1};streak=best_streak=longest_sec=-1;for(unsigned i=0;i<7;++i)daily[i]=(Count){-1,-1};}
}
static void received(int32_t code,const char *json,uint32_t size,void *u){
 (void)u;pending=0;int32_t schema=0;Window next[2][2];int32_t en[2],age[2];
 if(code!=200 || readnum(json,size,"schema",&schema) || schema!=1)goto bad;
 for(unsigned p=0;p<2;++p){
  if(readnum(json,size,p?"claude_enabled":"codex_enabled",&en[p]) || en[p]<0 || en[p]>1)goto bad;
  if(readnum(json,size,p?"claude_age":"codex_age",&age[p]) || age[p]<-1 || age[p]>604800)goto bad;
  for(unsigned w=0;w<2;++w){Window *v=&next[p][w];v->used=-1;v->minutes=v->reset=0;
   if(!en[p])continue;
   if(readnum(json,size,keys[p][w][0],&v->used))goto bad;
   if(v->used==-1)continue;
   if(v->used<0 || v->used>100 || readnum(json,size,keys[p][w][1],&v->minutes) || v->minutes<1 || v->minutes>525600 || readnum(json,size,keys[p][w][2],&v->reset) || v->reset<0 || v->reset>31536000)goto bad;
  }
 }
 for(unsigned p=0;p<2;++p){enabled[p]=en[p];ages[p]=age[p];for(unsigned w=0;w<2;++w)windows[p][w]=next[p][w];}
 read_detail(json,size);
 sample_data=0;readnum(json,size,"sample",&sample_data);
 received_data=1;elapsed=0;failed=0;render();return;
 bad:failed=1;if(code==429)cooldown=60;render();
}
static void refresh(void *u){
 (void)u;if(pending || cooldown)return;
 if(AGENT_USAGE_DEMO){enabled[0]=enabled[1]=1;ages[0]=ages[1]=0;
  windows[0][0]=(Window){28,300,7200};windows[0][1]=(Window){46,10080,259200};
  windows[1][0]=(Window){61,300,3600};windows[1][1]=(Window){23,10080,432000};
  detail_age=0;totals[0]=(Count){124,2};totals[1]=(Count){850,1};streak=8;best_streak=14;longest_sec=540;
  for(unsigned i=0;i<7;++i){daily[i]=(Count){120+(int32_t)i*90,1};day_dates[i]=1101+(int32_t)i;day_heights[i]=20+(int32_t)i*12;}
  for(unsigned i=0;i<3;++i){period_totals[i]=(Count){i==0?660:i==1?2730:7500,1};period_known[i]=period_expected[i]=i==0?1:i==1?7:30;}
  for(unsigned i=0;i<7;++i){monthly[i]=(Count){i<6?1000+(int)i*100:-1,1};month_heights[i]=i<6?50+(int)i*10:-1;}
  elapsed=failed=0;received_data=1;cooldown=5;render();return;
 }
 if(!endpoint[0]){char json[2049];int32_t n=host->companion_read(json,sizeof json);cooldown=5;if(n>0)received(200,json,(uint32_t)n,0);else render();return;}
 cooldown=5;pending=1;ui->text(status,"Refreshing...");
 if(host->http_get_json(endpoint,received,0)){pending=0;failed=1;render();}
}
static void choose_period(void *u){unsigned next=(unsigned)(uintptr_t)u;if(next>2 || next==period_view)return;period_view=next;render();}
static void tick(void *u){(void)u;if(cooldown)--cooldown;if(elapsed<604800)++elapsed;render();if(elapsed && elapsed%(endpoint[0]?60:5)==0)refresh(0);}
static void open_detail(void *u){(void)u;detail_view=1;render();}
static void close_app(void *u){(void)u;if(detail_view){detail_view=0;render();return;}host->request_close();}
static void shutdown(void){pending=0;host=0;}
static gadget_handle make(gadget_handle parent,unsigned kind,const char *text,int x,int y,int w,int h,unsigned style,gadget_callback cb,void *user){
 gadget_handle o=ui->create(parent,kind,text,cb,user);if(!o)return 0;
 if(ui->box(o,x,y,w,h)||ui->style(o,style))return 0;return o;
}
__attribute__((visibility("default"))) int gadget_app_entry(int argc,char **argv){
 if(argc!=2 || !argv)return -1;
 host=(const gadget_host_api*)argv[0];gadget_app_lifecycle *app=(gadget_app_lifecycle*)argv[1];
 if(!host || host->struct_size<offsetof(gadget_host_api,system_read) || host->major!=1 || host->minor<3 || !host->http_get_json || !host->json_number_scaled || !host->ui || !host->setting_get || !host->setting_set || !host->companion_read || !app || app->struct_size<sizeof(*app))return -1;
 ui=host->ui;if(ui->struct_size<offsetof(gadget_ui_api,chart))return -1;
 app->major=1;app->minor=3;app->shutdown=shutdown;
 detail_view=0;period_view=1;read_detail("{}",2);
 pending=cooldown=elapsed=received_data=failed=0;sample_data=0;
 endpoint[0]=0;if(host->setting_get("endpoint",endpoint,sizeof endpoint)<0){unsigned n=0;while(AGENT_USAGE_URL[n] && n<255){endpoint[n]=AGENT_USAGE_URL[n];++n;}endpoint[n]=0;}
 for(unsigned p=0;p<2;++p){enabled[p]=(endpoint[0]||AGENT_USAGE_DEMO||p==0);ages[p]=-1;for(unsigned w=0;w<2;++w)windows[p][w]=(Window){-1,0,0};}
 gadget_handle root=ui->create(0,GADGET_UI_ROOT,0,0,0);if(!root)return -1;
 gadget_handle header=make(root,GADGET_UI_CONTAINER,0,0,0,320,44,GADGET_STYLE_HEADER,0,0);
 if(!header || !make(header,GADGET_UI_BUTTON,0,0,0,48,44,GADGET_STYLE_BACK,close_app,0) || !make(header,GADGET_UI_BUTTON,0,272,0,48,44,GADGET_STYLE_REFRESH,refresh,0))return -1;
 heading=make(header,GADGET_UI_LABEL,"Agent Usage",48,8,224,30,GADGET_STYLE_TITLE,0,0);
 if(heading)ui->align(heading,1);
 for(unsigned p=0;p<2;++p){Card *c=&cards[p];
  c->box=make(root,GADGET_UI_CONTAINER,0,10,56+(int)p*172,300,160,GADGET_STYLE_CARD,0,0);
  if(!c->box)return -1;
  c->name=make(c->box,p?GADGET_UI_LABEL:GADGET_UI_BUTTON,p?names[p]:"Codex >",14,8,114,38,GADGET_STYLE_TITLE,p?0:open_detail,0);
  c->freshness=make(c->box,GADGET_UI_LABEL,"",134,21,46,18,GADGET_STYLE_MUTED,0,0);
  c->value=make(c->box,GADGET_UI_LABEL,"--",184,8,102,40,GADGET_STYLE_VALUE,0,0);
  if(c->value)ui->align(c->value,2);
  if(p)ui->style(c->value,GADGET_STYLE_BLUE);
  c->primary=make(c->box,GADGET_UI_LABEL,"",14,49,274,20,GADGET_STYLE_MUTED,0,0);
  c->secondary=make(c->box,GADGET_UI_LABEL,"",14,94,274,20,GADGET_STYLE_DEFAULT,0,0);
  c->reset=make(c->box,GADGET_UI_LABEL,"",14,136,274,20,GADGET_STYLE_MUTED,0,0);
  c->bars[0]=make(c->box,GADGET_UI_BAR,0,14,74,272,8,p?GADGET_STYLE_BLUE:GADGET_STYLE_DEFAULT,0,0);
  c->bars[1]=make(c->box,GADGET_UI_BAR,0,14,118,272,6,p?GADGET_STYLE_BLUE:GADGET_STYLE_DEFAULT,0,0);
  if(!c->name||!c->freshness||!c->value||!c->primary||!c->secondary||!c->reset||!c->bars[0]||!c->bars[1])return -1;
 }
 detail_root=make(root,GADGET_UI_CONTAINER,0,0,54,320,394,GADGET_STYLE_DEFAULT,0,0);if(!detail_root)return -1;
 const char *period_names[]={"Today","7 days","Month"};
 for(unsigned i=0;i<3;++i){tabs[i]=make(detail_root,GADGET_UI_BUTTON,period_names[i],10+(int)i*100,0,100,38,GADGET_STYLE_TAB,choose_period,(void*)(uintptr_t)i);if(!tabs[i])return -1;}
 gadget_handle chart=make(detail_root,GADGET_UI_CONTAINER,0,10,46,300,152,GADGET_STYLE_CARD,0,0);if(!chart)return -1;
 chart_title=make(chart,GADGET_UI_LABEL,"",12,10,276,22,GADGET_STYLE_MUTED,0,0);if(!chart_title)return -1;
 for(unsigned i=0;i<7;++i){
  day_values[i]=make(chart,GADGET_UI_LABEL,"",7+(int)i*41,36,40,20,GADGET_STYLE_DEFAULT,0,0);
  day_bars[i]=make(chart,GADGET_UI_BAR,0,18+(int)i*41,60,18,48,GADGET_STYLE_DEFAULT,0,0);
  day_labels[i]=make(chart,GADGET_UI_LABEL,"",7+(int)i*41,118,40,20,GADGET_STYLE_MUTED,0,0);
  if(!day_values[i]||!day_bars[i]||!day_labels[i])return -1;ui->align(day_values[i],1);ui->align(day_labels[i],1);
 }
 const char *stat_names[]={"Lifetime tokens","Peak day tokens","Current streak","Best streak"};
 for(unsigned i=0;i<4;++i){gadget_handle card=make(detail_root,GADGET_UI_CONTAINER,0,10+(int)(i%2)*156,204+(int)(i/2)*72,144,68,GADGET_STYLE_CARD,0,0);if(!card)return -1;
  if(!make(card,GADGET_UI_LABEL,stat_names[i],10,8,124,20,GADGET_STYLE_MUTED,0,0))return -1;
  stat_values[i]=make(card,GADGET_UI_LABEL,"",10,30,124,32,GADGET_STYLE_TITLE,0,0);if(!stat_values[i])return -1;
 }
 longest_label=make(detail_root,GADGET_UI_LABEL,"",14,354,292,24,GADGET_STYLE_MUTED,0,0);if(!longest_label)return -1;
 status=make(root,GADGET_UI_LABEL,"Starting...",14,454,292,20,GADGET_STYLE_MUTED,0,0);
 if(!heading || !tabs[0] || !tabs[1] || !tabs[2] || !status || !host->timer_create(1000,tick,0))return -1;
 render();refresh(0);return 0;
}
