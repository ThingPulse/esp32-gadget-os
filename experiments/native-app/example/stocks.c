/* Stocks owns its watchlist, Yahoo adapter, scheduler and UI. Only bounded
 * SDK primitives cross the package boundary; no firmware StockService. */
#ifndef GADGET_APP_H
#include "gadget_app.h"
#endif
#include <stddef.h>
#define STOCKS_MAX 10
#define HISTORY_MAX 96
#define BG 0x172131
#define MUTED 0xaebdcd
#define GREEN 0x63e6be
#define RED 0xff8787
static const gadget_host_api *host;
static const gadget_ui_api *ui;
static const gadget_services_api *svc;
static gadget_handle root,body,input,error_label;
typedef struct {
 char symbol[25],name[81],currency[9],exchange[41],error[100];
 double price,previous,history[HISTORY_MAX];
 uint32_t stamp,fetched,attempt,due;
 int valid,has_previous,period,delay,count,failures,attempted;
} Stock;
static Stock stocks[STOCKS_MAX];
static unsigned count,selected,generation,request_generation,request_index;
static int period,view,settings_entry,busy,request_period,closing,dirty,german;
static uint32_t next_request,last_paint,notice_until;
static char notice[100],editor_error[100];
static int editor_return;
static const char *defaults="AAPL\nNESN.SW\n7203.T\nSAP.DE\nVOD.L";
static unsigned length(const char*s){unsigned n=0;while(s&&s[n])++n;return n;}
static void copy(char*d,const char*s,unsigned cap){unsigned i=0;if(!cap)return;while(s&&s[i]&&i+1<cap){d[i]=s[i];++i;}d[i]=0;}
static void append(char*d,const char*s,unsigned cap){unsigned n=length(d);if(n<cap)copy(d+n,s,cap-n);}
static int equal(const char*a,const char*b){while(*a&&*a==*b){++a;++b;}return *a==*b;}
static void zero(void*p,unsigned n){unsigned char*b=(unsigned char*)p;while(n--)*b++=0;}
static const char *tr(const char*en,const char*de){return german?de:en;}
static int ready(uint32_t now,uint32_t due){return (int32_t)(now-due)>=0;}
static void number(char*out,double value,unsigned digits){if(svc->format_number(value,digits,out,64)<0)copy(out,"--",64);}
static void integer(char*out,unsigned n){char rev[12];unsigned len=0,i=0;do{rev[len++]=(char)('0'+n%10);n/=10;}while(n);while(len)out[i++]=rev[--len];out[i]=0;}
static void money(char*out,double n,const char*currency){unsigned places=equal(currency,"JPY")?0:(n>0&&n<1?6:2);number(out,n,places);append(out," ",80);append(out,currency,80);}
static void change(char*out,const Stock*s){if(!s->has_previous){copy(out,tr("Change unavailable","Aenderung unbekannt"),80);return;}double p=(s->price-s->previous)/s->previous*100.0;number(out,p,2);if(p>0){char tmp[80];copy(tmp,out,80);copy(out,"+",80);append(out,tmp,80);}append(out,"%",80);}
static const char *validate(const char*text,char symbols[STOCKS_MAX][25],unsigned *size){
 unsigned n=0;const char*p=text;
 while(*p){while(*p==' '||*p=='\n'||*p=='\r'||*p=='\t')++p;if(!*p)break;
  if(n==STOCKS_MAX)return tr("Maximum 10 symbols","Maximal 10 Symbole");
  unsigned k=0;while(*p&&*p!=' '&&*p!='\n'&&*p!='\r'&&*p!='\t'){
   char c=*p++;if(c>='a'&&c<='z')c-=32;
   if(k==24||!((c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='.'||c=='-')||(!k&&(c=='.'||c=='-')))
    return tr("Use symbols such as AAPL or BTC-USD","Symbole wie AAPL oder BTC-USD verwenden");
   symbols[n][k++]=c;
  }symbols[n][k]=0;for(unsigned j=0;j<n;++j)if(equal(symbols[j],symbols[n]))return tr("Duplicate symbol","Doppeltes Symbol");++n;
 }*size=n;return 0;
}
static void watchlist(char*out){out[0]=0;for(unsigned i=0;i<count;++i){if(i)append(out,"\n",256);append(out,stocks[i].symbol,256);}}
static void apply_symbols(char symbols[STOCKS_MAX][25],unsigned n){
 /* Quotes are transient; no callback may write into this generation afterward. */
 ++generation;count=n;selected=0;zero(stocks,sizeof(stocks));uint32_t now=svc->monotonic_ms();
 for(unsigned i=0;i<n;++i){copy(stocks[i].symbol,symbols[i],25);stocks[i].period=-1;stocks[i].due=now;}
}
static void message(const char*s){copy(notice,s,sizeof(notice));notice_until=svc->monotonic_ms()+4000;dirty=1;}
static void back(void*unused){(void)unused;if(view==2){if(settings_entry){host->request_close();return;}view=editor_return;}else if(view==1)view=0;else{host->request_close();return;}dirty=1;}
static void edit(void*unused){(void)unused;editor_return=view;view=2;editor_error[0]=0;dirty=1;}
static void save(void*unused){(void)unused;char text[256],symbols[STOCKS_MAX][25],normalized[256];unsigned n=0;
 if(ui->input_get(input,text,sizeof(text))<0){ui->text(error_label,tr("Watchlist is too long","Liste ist zu lang"));return;}
 const char*why=validate(text,symbols,&n);if(why){ui->text(error_label,why);return;}
 normalized[0]=0;for(unsigned i=0;i<n;++i){if(i)append(normalized,"\n",256);append(normalized,symbols[i],256);}
 if(host->setting_set("symbols",normalized)<0){ui->text(error_label,tr("Could not save watchlist","Liste konnte nicht gespeichert werden"));return;}
 host->setting_set("import_error","");apply_symbols(symbols,n);
 if(settings_entry){host->request_close();return;}view=0;dirty=1;
}
static void select_stock(void*data){selected=(unsigned)(uintptr_t)data;view=1;dirty=1;}
static void select_period(void*data){period=(int)(uintptr_t)data;dirty=1;}
static void navigate(void*data){if(count)selected=(selected+count+((intptr_t)data<0?-1:1))%count;dirty=1;}
static void refresh(void*unused){(void)unused;uint32_t now=svc->monotonic_ms();int queued=0;
 for(unsigned i=0;i<count;++i)if(view==0||i==selected){Stock*s=&stocks[i];if((!s->attempted||now-s->attempt>=30000)&&(!s->failures||ready(now,s->due))){s->due=now;queued=1;}}
 message(queued?tr("Refresh queued","Aktualisierung vorgemerkt"):tr("Please wait before refreshing again","Bitte vor erneutem Laden warten"));
}
static void gesture(uint32_t direction,void*unused){(void)unused;if(view==0&&direction==GADGET_GESTURE_PULL)refresh(0);else if(view==1&&direction==GADGET_GESTURE_LEFT)navigate((void*)1);else if(view==1&&direction==GADGET_GESTURE_RIGHT)navigate((void*)(intptr_t)-1);}
static gadget_handle widget(gadget_handle parent,unsigned kind,const char*text,int x,int y,int w,int h,gadget_callback cb,void*data){gadget_handle o=ui->create(parent,kind,text,cb,data);ui->box(o,x,y,w,h);return o;}
static gadget_handle label(gadget_handle p,const char*t,int x,int y,int w,int h,unsigned style){gadget_handle o=widget(p,GADGET_UI_LABEL,t,x,y,w,h,0,0);ui->style(o,style);ui->style(o,GADGET_STYLE_ELLIPSIS);return o;}
static gadget_handle button(gadget_handle p,const char*t,int x,int y,int w,int h,gadget_callback cb,void*data){return widget(p,GADGET_UI_BUTTON,t,x,y,w,h,cb,data);}
static void render(void){
 int old_scroll=view==0&&body?ui->scroll_y(body,-1):0;if(old_scroll<0)old_scroll=0;
 ui->clear(root);body=input=error_label=0;
 gadget_handle bar=widget(root,GADGET_UI_CONTAINER,"",0,0,320,44,0,0);ui->style(bar,GADGET_STYLE_HEADER);
 gadget_handle b=button(bar,"",0,0,48,44,back,0);ui->style(b,GADGET_STYLE_BACK);
 label(bar,tr("Stocks","Aktien"),58,10,126,28,GADGET_STYLE_DEFAULT);
 if(view!=2){b=button(bar,"",220,0,44,44,refresh,0);ui->style(b,GADGET_STYLE_REFRESH);button(bar,tr("Edit","Liste"),266,0,54,44,edit,0);}
 if(view==2){
  label(root,tr("One symbol per line (maximum 10)","Ein Symbol pro Zeile (maximal 10)"),12,52,296,22,GADGET_STYLE_DEFAULT);
  label(root,tr("Reorder lines to reorder your watchlist","Zeilen umordnen fuer die Reihenfolge"),12,77,296,22,GADGET_STYLE_MUTED);
  char text[256];watchlist(text);input=widget(root,GADGET_UI_TEXTAREA,text,12,104,296,102,0,0);ui->input_config(input,255,1);
  error_label=label(root,editor_error,12,210,296,30,GADGET_STYLE_MUTED);
  button(root,tr("Save","Speichern"),12,246,142,40,save,0);button(root,tr("Cancel","Abbrechen"),166,246,142,40,back,0);
  gadget_handle keyboard=widget(root,GADGET_UI_KEYBOARD,"",0,292,320,188,0,0);ui->keyboard_attach(keyboard,input);return;
 }
 body=widget(root,GADGET_UI_CONTAINER,"",0,48,320,394,0,0);ui->gestures(body,gesture,0);
 if(view==0){
  ui->scroll(body,1);
  if(!count)label(body,tr("Your watchlist is empty. Tap Edit.","Liste ist leer. Auf Liste tippen."),12,14,296,44,GADGET_STYLE_MUTED);
  for(unsigned i=0;i<count;++i){Stock*s=&stocks[i];gadget_handle row=button(body,"",12,8+(int)i*74,296,66,select_stock,(void*)(uintptr_t)i);
   label(row,s->symbol,10,6,108,24,GADGET_STYLE_DEFAULT);char price[80],delta[80];
   if(s->valid){money(price,s->price,s->currency);change(delta,s);}else{copy(price,"--",80);copy(delta,s->error[0]?s->error:tr("Waiting for quote","Warte auf Kurs"),80);}
   label(row,price,122,6,164,24,GADGET_STYLE_DEFAULT);gadget_handle d=label(row,delta,s->valid?122:10,34,s->valid?164:276,24,GADGET_STYLE_MUTED);
   if(s->valid&&s->has_previous)ui->paint(d,s->price>=s->previous?GREEN:RED,0,0);
   if(s->valid&&s->error[0])label(row,tr("Cached","Cache"),10,34,100,24,GADGET_STYLE_MUTED);
  }
  /* LVGL only recognizes elastic pulls when the content has a scroll extent. */
  widget(body,GADGET_UI_LABEL,"",0,394,1,1,0,0);
  ui->scroll_y(body,old_scroll);
 }else if(count){Stock*s=&stocks[selected];char text[100],price[80],delta[80];
  label(body,s->symbol,12,0,200,30,GADGET_STYLE_TITLE);label(body,s->valid?s->name:"",12,34,296,22,GADGET_STYLE_MUTED);
  if(s->valid){money(price,s->price,s->currency);change(delta,s);}else{copy(price,"--",80);delta[0]=0;}
  label(body,price,12,62,296,34,GADGET_STYLE_TITLE);gadget_handle d=label(body,delta,12,99,296,24,GADGET_STYLE_DEFAULT);ui->paint(d,s->has_previous?(s->price>=s->previous?GREEN:RED):MUTED,0,0);
  label(body,s->exchange,12,126,296,22,GADGET_STYLE_MUTED);
  int32_t points[HISTORY_MAX];double low=0,high=0;unsigned found=0;
  if(s->valid&&s->period==period){for(int i=0;i<s->count;++i)if(s->history[i]>0){if(!found||s->history[i]<low)low=s->history[i];if(!found||s->history[i]>high)high=s->history[i];++found;}}
  if(found){for(int i=0;i<s->count;++i)points[i]=s->history[i]<=0?-1:high==low?50:(int32_t)((s->history[i]-low)/(high-low)*100.0);
   gadget_handle chart=widget(body,GADGET_UI_CHART,"",12,157,296,100,0,0);ui->chart_fit(chart,points,s->count);
  }else label(body,tr("History unavailable / loading","Verlauf nicht verfuegbar / laedt"),12,192,296,24,GADGET_STYLE_MUTED);
  const char*periods[]={"5D","1M","1Y"};for(int i=0;i<3;++i){b=button(body,periods[i],12+i*100,268,96,34,select_period,(void*)(uintptr_t)i);ui->selected(b,period==i);}
  text[0]=0;if(s->valid)svc->format_time(s->stamp,2,text,sizeof(text));label(body,text,12,308,296,22,GADGET_STYLE_MUTED);
  if(s->has_previous){money(price,s->previous,s->currency);copy(text,tr("Prev close: ","Vortag: "),100);append(text,price,100);}else copy(text,tr("Previous close unavailable","Vortageskurs nicht verfuegbar"),100);
  label(body,text,12,332,296,22,GADGET_STYLE_MUTED);
  button(body,"<",12,360,64,32,navigate,(void*)(intptr_t)-1);button(body,">",244,360,64,32,navigate,(void*)1);
 }
 const char *status=notice[0]?notice:busy?tr("Loading...","Lade..."):tr("Yahoo Finance - quotes may be delayed","Yahoo Finance - Kurse ggf. verzoegert");
 char delayed[100];
 if(!notice[0]&&view==1&&count){Stock*s=&stocks[selected];
  if(s->error[0])status=s->error;
  else if(s->valid&&svc->monotonic_ms()-s->fetched>900000)status=tr("Cached - refresh pending","Cache - Aktualisierung ausstehend");
  else if(!busy&&s->valid&&s->delay>0){integer(delayed,s->delay);append(delayed,tr(" min delayed"," Min. verzoegert"),sizeof(delayed));status=delayed;}
 }
 label(root,status,12,448,296,28,GADGET_STYLE_MUTED);
}
static void path(char*out,const char*suffix){copy(out,"chart/result/0/",128);append(out,suffix,128);}
static int real(const char*suffix,double*out){char p[128];path(p,suffix);return svc->json_real(p,out)==0;}
static int string(const char*suffix,char*out,unsigned cap){char p[128];path(p,suffix);if(svc->json_get(p,out,cap)<0){out[0]=0;return 0;}return 1;}
static void array_path(char*out,const char*prefix,unsigned i){char n[12];integer(n,i);copy(out,prefix,128);append(out,n,128);}
static int parse_quote(const char*json,unsigned size,Stock*out){
 if(svc->json_open(json,size)<0)return 0;
 char text[128],p[128];double value,offset=0;int ok=0;
 if(svc->json_count("chart/result")!=1||svc->json_get("chart/error/code",text,sizeof(text))>=0)goto done;
 if(!string("meta/symbol",text,sizeof(text))||!equal(text,out->symbol)||!real("meta/regularMarketPrice",&value)||value<=0||value>1e12)goto done;out->price=value;
 if(!real("meta/regularMarketTime",&value)||value<1||value>4294967295.0||value!=(double)(uint32_t)value)goto done;out->stamp=(uint32_t)value;
 if(!string("meta/currency",out->currency,sizeof(out->currency))||!out->currency[0])goto done;
 for(unsigned i=0;out->currency[i];++i)if(!((out->currency[i]>='A'&&out->currency[i]<='Z')||(out->currency[i]>='a'&&out->currency[i]<='z')))goto done;
 if(!string("meta/longName",out->name,sizeof(out->name))&&!string("meta/shortName",out->name,sizeof(out->name)))copy(out->name,out->symbol,sizeof(out->name));
 if(!string("meta/fullExchangeName",out->exchange,sizeof(out->exchange)))string("meta/exchangeName",out->exchange,sizeof(out->exchange));
 out->has_previous=real("meta/previousClose",&value)&&value>0&&value<=1e12;if(out->has_previous)out->previous=value;
 out->delay=real("meta/exchangeDataDelayedBy",&value)&&value>=0&&value<10000?(int)value:0;
 {
  int n=svc->json_count("chart/result/0/timestamp");if(n<0||n>400||svc->json_count("chart/result/0/indicators/quote/0/close")!=n)goto done;
  int has_offset=real("meta/gmtoffset",&offset)&&offset>=-86400&&offset<=86400;
  double previous_time=0;out->count=n<HISTORY_MAX?n:HISTORY_MAX;unsigned sample=0;
  for(int i=0;i<n;++i){double stamp,close=0;array_path(p,"chart/result/0/timestamp/",i);
   if(svc->json_real(p,&stamp)<0||stamp<=previous_time||stamp>4294967295.0||stamp!=(double)(uint32_t)stamp)goto done;previous_time=stamp;
   array_path(p,"chart/result/0/indicators/quote/0/close/",i);if(svc->json_real(p,&close)<0||close<=0||close>1e12)close=-1;
   if(!out->has_previous&&has_offset&&close>0&&(int)((stamp+offset)/86400)<(int)(((double)out->stamp+offset)/86400))out->previous=close;
   if(sample<(unsigned)out->count&&(out->count<=1||i==(int)(sample*(n-1)/(out->count-1))))out->history[sample++]=close;
  }
  if(!out->has_previous&&out->previous>0)out->has_previous=1;
 }
 out->valid=1;out->period=request_period;ok=1;
 done:svc->json_close();return ok;
}
static void received(int32_t status,const char*json,uint32_t size,void*unused){
 (void)unused;busy=0;if(closing||request_generation!=generation||request_index>=count)return;
 Stock*s=&stocks[request_index],fresh;zero(&fresh,sizeof(fresh));copy(fresh.symbol,s->symbol,25);
 uint32_t now=svc->monotonic_ms();
 if(status==200&&parse_quote(json,size,&fresh)){fresh.attempt=s->attempt;fresh.attempted=1;fresh.fetched=now;fresh.due=now+300000;*s=fresh;}
 else{unsigned delay=60000U<<(s->failures<9?s->failures:9);if(delay>21600000)delay=21600000;if(s->failures<10)++s->failures;
  if(status==404||status==401||status==403)delay=21600000;
  unsigned retry=svc->http_retry_seconds();if(retry>21600)retry=21600;if(retry*1000>delay)delay=retry*1000;s->due=now+delay;
  const char*why=status==429?tr("Yahoo rate limit","Yahoo-Anfragelimit"):status==404?tr("Symbol not found","Symbol nicht gefunden"):status==401||status==403?tr("Yahoo denied access","Yahoo verweigert Zugriff"):status==200?tr("Invalid provider response","Ungueltige Kursdaten"):status==GADGET_HTTP_OFFLINE?tr("Offline - waiting for Wi-Fi","Offline - warte auf WLAN"):tr("Quote request failed","Kursabfrage fehlgeschlagen");
  copy(s->error,s->valid?tr("Cached: ","Cache: "):"",sizeof(s->error));append(s->error,why,sizeof(s->error));
 }if(view!=2)dirty=1;
}
static void tick(void*unused){
 (void)unused;if(closing)return;uint32_t now=svc->monotonic_ms();
 if(notice[0]&&ready(now,notice_until)){notice[0]=0;if(view!=2)dirty=1;}
 if(view!=2&&!busy&&count&&ready(now,next_request)){
  int pick=-1;
  if(view==1){Stock*s=&stocks[selected];if(ready(now,s->due)||(s->period!=period&&!s->failures))pick=(int)selected;}
  else for(unsigned i=0;i<count;++i)if(ready(now,stocks[i].due)){pick=(int)i;break;}
  if(pick>=0){Stock*s=&stocks[pick];char url[256];copy(url,"https://query1.finance.yahoo.com/v8/finance/chart/",sizeof(url));append(url,s->symbol,sizeof(url));append(url,"?range=",sizeof(url));append(url,view==1?(period==0?"5d":period==1?"1mo":"1y"):"5d",sizeof(url));append(url,"&interval=1d&includePrePost=false",sizeof(url));
   request_index=(unsigned)pick;request_period=view==1?period:0;request_generation=generation;busy=1;next_request=now+5000;
   int result=svc->http_get(url,65536,received,0);
   if(result<0){busy=0;if(result!=GADGET_HTTP_BUSY&&result!=GADGET_HTTP_RATE_LIMIT){s->attempt=now;s->attempted=1;received(result,0,0,0);}}
   else{s->attempt=now;s->attempted=1;}dirty=1;
  }
 }
 if(view!=2&&now-last_paint>=60000){last_paint=now;dirty=1;}
 if(dirty&&ui->interaction_active(body)<=0){dirty=0;render();}
}
static void shutdown_app(void){closing=1;root=body=input=error_label=0;}
__attribute__((visibility("default")))
int gadget_app_entry(int argc,char**argv){
 if(argc!=2||!argv)return -1;host=(const gadget_host_api*)argv[0];gadget_app_lifecycle*app=(gadget_app_lifecycle*)argv[1];
 if(!host||host->major!=1||host->minor<9||host->struct_size<sizeof(*host)||!host->ui||host->ui->struct_size<sizeof(gadget_ui_api)||!host->services||host->services->struct_size<sizeof(gadget_services_api)||!app||app->struct_size<sizeof(*app))return -2;
 app->major=1;app->minor=9;app->shutdown=shutdown_app;ui=host->ui;svc=host->services;closing=busy=0;generation=0;period=0;body=input=error_label=0;notice[0]=editor_error[0]=0;next_request=svc->monotonic_ms();last_paint=next_request;
 gadget_environment env;zero(&env,sizeof(env));env.struct_size=sizeof(env);svc->environment(&env);german=env.german;
 settings_entry=host->launch_mode()==GADGET_LAUNCH_SETTINGS;view=settings_entry?2:0;editor_return=0;
 char text[256],symbols[STOCKS_MAX][25],migration[100];unsigned n=0;
 if(host->setting_get("symbols",text,sizeof(text))<0)copy(text,defaults,sizeof(text));
 const char*why=validate(text,symbols,&n);if(why){copy(editor_error,why,sizeof(editor_error));n=0;view=2;}
 apply_symbols(symbols,n);
 if(host->setting_get("import_error",migration,sizeof(migration))>0){copy(editor_error,migration,sizeof(editor_error));view=2;}
 root=ui->create(0,GADGET_UI_ROOT,"",0,0);if(!root||ui->reserve(96)<0||!host->timer_create(250,tick,0))return -3;dirty=0;render();return 0;
}
#ifdef __XTENSA__
void *memcpy(void *dest,const void *src,size_t n){unsigned char*d=dest;const unsigned char*s=src;while(n--)*d++=*s++;return dest;}
#endif
#include "../sdk/gadget_math.h"
