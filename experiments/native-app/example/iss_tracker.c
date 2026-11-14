/* Independent pass-prediction app. No firmware ISS service or LVGL linkage. */
#ifndef GADGET_APP_H
#include "gadget_app.h"
#endif
#include <stddef.h>
#if defined(__XTENSA__)
void *memcpy(void*d,const void*s,size_t n){unsigned char*a=d;const unsigned char*b=s;while(n--)*a++=*b++;return d;}
void *memset(void*d,int c,size_t n){unsigned char*a=d;while(n--)*a++=(unsigned char)c;return d;}
#endif
static const gadget_host_api *host;
static const gadget_ui_api *ui;
static const gadget_services_api *svc;
typedef struct {char s[160];unsigned n;} Text;
static void add(Text*t,const char*s){while(*s&&t->n<159)t->s[t->n++]=*s++;t->s[t->n]=0;}
static void number(Text*t,unsigned n){char b[11];unsigned i=0;do{b[i++]=(char)('0'+n%10);n/=10;}while(n);while(i&&t->n<159)t->s[t->n++]=b[--i];t->s[t->n]=0;}
static int equal(const char*a,const char*b){while(*a&&*a==*b){++a;++b;}return *a==*b;}
static void coordinate(Text*t,int32_t n){if(n<0){add(t,"-");n=-n;}number(t,(unsigned)n/1000000);add(t,".");unsigned rest=(unsigned)n%1000000;for(unsigned div=100000;div;div/=10){char c[2]={(char)('0'+rest/div),0};add(t,c);rest%=div;}}
static void duration(Text*t,uint32_t n){if(n>=86400){number(t,n/86400);add(t,"d ");number(t,n%86400/3600);add(t,"h");}else if(n>=3600){number(t,n/3600);add(t,"h ");if(n%3600/60<10)add(t,"0");number(t,n%3600/60);add(t,"m");}else{number(t,n/60);add(t,"m ");if(n%60<10)add(t,"0");number(t,n%60);add(t,"s");}}
static gadget_environment env;
#include "iss_tracker_messages.inc"
static const char*T(const char*s){if(env.german)for(unsigned i=0;i<sizeof(messages)/sizeof(messages[0]);++i)if(equal(s,messages[i].en))return messages[i].de;return s;}
typedef struct {uint32_t rise,peak,set,start,end;int32_t rise_az,set_az,elevation;uint32_t visible;} Pass;
typedef struct {uint32_t magic;int32_t lat,lon;uint32_t fetched,stale,count;Pass passes[40];} Forecast;
static Forecast forecast,working;
static uint32_t now,selected,allowed,backoff,next_refresh,stage,job_lat,job_lon,revision,shown_revision,list_expiry,ticks;
static int fetching,pending,passes_page,visible_only,closing;
static const char*error;
static gadget_handle root,body,location,status,tabs[2];
#define BG 0x1e2030
#define WHITE 0xe7eaf4
#define MINT 0x63e6be
#define MUTED 0x9aa5bd
#define RIGHT "\xef\x81\x94"
static void render(void);
static const char*direction(int32_t a){static const char*n[]={"N","NNE","NE","ENE","E","ESE","SE","SSE","S","SSW","SW","WSW","W","WNW","NW","NNW"};return n[((a+112)/225)&15];}
static void directions(Text*t,const Pass*p){add(t,direction(p->rise_az));add(t," " RIGHT " ");add(t,direction(p->set_az));}
static void date(Text*t,uint32_t stamp,unsigned mode){char s[80];if(svc->format_time(stamp,mode,s,sizeof(s))>=0)add(t,s);}
static gadget_handle make(gadget_handle parent,unsigned kind,int x,int y,int w,int h,const char*s,gadget_callback cb,void*u){gadget_handle o=ui->create(parent,kind,s,cb,u);if(!o){closing=1;host->request_close();return 0;}ui->box(o,x,y,w,h);return o;}
static gadget_handle label(gadget_handle p,int x,int y,int w,int h,const char*s,unsigned color,unsigned font,int center){gadget_handle o=make(p,GADGET_UI_LABEL,x,y,w,h,s,0,0);if(font)ui->style(o,font);ui->paint(o,color,0,0);if(center)ui->align(o,1);ui->wrap(o);return o;}
static gadget_handle button(gadget_handle p,int x,int y,int w,int h,const char*s,gadget_callback cb,void*u,int active){gadget_handle o=make(p,GADGET_UI_BUTTON,x,y,w,h,T(s),cb,u);ui->paint(o,active?MINT:WHITE,active?0x285c55:0x293549,100);return o;}
static void freshness(Text*t){if(fetching||(pending&&env.online&&env.clock_ready&&ticks>=allowed&&!error))add(t,T("Updating predictions..."));else if(error)add(t,T(error));else if(forecast.stale||(forecast.fetched&&now-forecast.fetched>21600))add(t,T("Old predictions - refresh needed"));else if(!env.online)add(t,T(forecast.fetched?"Offline - cached predictions":"Connect to Wi-Fi for predictions"));else if(!env.clock_ready)add(t,T("Waiting for clock sync"));else if(forecast.fetched){add(t,T("Updated"));add(t," ");duration(t,now>=forecast.fetched?now-forecast.fetched:0);add(t," ");add(t,T("ago"));}else add(t,T("Waiting for predictions"));}
static void footer(int y){Text t={{0},0};freshness(&t);status=label(body,0,y,300,20,t.s,MUTED,0,1);ui->style(status,GADGET_STYLE_ELLIPSIS);}
static void open_location(void*u){(void)u;svc->open_location();}
static void close_app(void*u){(void)u;closing=1;host->request_close();}
static void tab(void*u){passes_page=u!=0;selected=0;shown_revision=~0U;render();}
static void filter(void*u){visible_only=u==0;shown_revision=~0U;render();}
static void choose(void*u){unsigned i=(unsigned)(uintptr_t)u;if(i<forecast.count){selected=forecast.passes[i].rise;passes_page=0;shown_revision=~0U;render();}}
static void refresh(void*u){(void)u;if(!env.has_location)return;pending=1;if(ticks<allowed)error="Refresh queued - please wait";++revision;render();}
static Pass*chosen(void){for(unsigned i=0;i<forecast.count;++i)if(forecast.passes[i].rise==selected&&forecast.passes[i].set>now)return &forecast.passes[i];selected=0;for(unsigned i=0;i<forecast.count;++i){Pass*p=&forecast.passes[i];if(p->visible&&p->rise<=now&&p->set>now)return p;}for(unsigned i=0;i<forecast.count;++i){Pass*p=&forecast.passes[i];if(p->visible&&p->set>now&&p->end>now)return p;}return 0;}
/* Fixed-point sine for compass endpoint placement: one-degree lookup, no float ABI. */
#include "iss_tracker_sine.inc"
static int sine(int deg){deg%=360;if(deg<0)deg+=360;int sign=deg>180?-1:1;if(deg>180)deg-=180;if(deg>90)deg=180-deg;return sign*sin90[deg];}
static void compass(const Pass*p){gadget_handle c=make(body,GADGET_UI_CONTAINER,0,98,300,140,0,0,0);gadget_draw_command cmds[3]={{GADGET_DRAW_CIRCLE,150,70,52,0,1,0x65748a}};int angles[2]={p->rise_az/10,p->set_az/10};for(int i=0;i<2;++i){cmds[i+1]=(gadget_draw_command){GADGET_DRAW_CIRCLE,150+sine(angles[i])*52/1000,70-sine(angles[i]+90)*52/1000,5,0,5,MINT};}ui->draw(c,cmds,3);label(c,140,0,20,20,"N",MUTED,0,1);label(c,140,123,20,17,"S",MUTED,0,1);label(c,73,61,18,20,"W",MUTED,0,1);label(c,211,61,18,20,"E",MUTED,0,1);}
static void satellite(void){gadget_handle c=make(body,GADGET_UI_CONTAINER,0,137,300,42,0,0,0);gadget_draw_command d[26];unsigned n=0;d[n++]=(gadget_draw_command){0,112,20,188,20,2,0x65748a};d[n++]=(gadget_draw_command){0,150,7,150,33,4,MUTED};for(int side=-1;side<=1;side+=2)for(int row=-1;row<=1;row+=2){int x=150+side*23-9,y=20+row*11-7;d[n++]=(gadget_draw_command){0,x,y,x+18,y,2,0x65748a};d[n++]=(gadget_draw_command){0,x,y+14,x+18,y+14,2,0x65748a};for(int k=0;k<=3;++k)d[n++]=(gadget_draw_command){0,x+k*6,y,x+k*6,y+14,1,0x65748a};}ui->draw(c,d,n);}
static void render(void){if(closing||!body)return;if(passes_page&&shown_revision==revision&&now<list_expiry){Text t={{0},0};freshness(&t);if(status)ui->text(status,t.s);return;}shown_revision=revision;status=0;ui->clear(body);for(unsigned i=0;i<2;++i)ui->paint(tabs[i],WHITE,i==(unsigned)passes_page?0x285c55:BG,100);ui->text(location,env.has_location?env.location:T("Set your location"));
 if(!env.has_location){label(body,12,45,276,40,T("Set your location"),WHITE,GADGET_STYLE_TITLE,1);label(body,16,95,268,90,T("ISS predictions use your Weather location, or your Planes location if Weather is not configured."),MUTED,0,1);button(body,35,195,230,48,"Set location",open_location,0,0);return;}
 if(!env.clock_ready){label(body,15,95,270,80,T("Waiting for clock sync"),WHITE,GADGET_STYLE_TITLE,1);return;}
 if(passes_page){label(body,10,3,280,30,T("Upcoming passes"),WHITE,GADGET_STYLE_TITLE,1);button(body,10,36,136,44,"Visible",filter,0,visible_only);button(body,154,36,136,44,"All",filter,(void*)1,!visible_only);gadget_handle list=make(body,GADGET_UI_CONTAINER,0,86,300,220,0,0,0);ui->scroll(list,1);unsigned row=0;list_expiry=now+3600;
 for(unsigned i=0;i<forecast.count;++i){Pass*p=&forecast.passes[i];if(p->set<=now||(visible_only&&(!p->visible||p->end<=now)))continue;uint32_t expiry=visible_only?p->end:p->set;if(expiry<list_expiry)list_expiry=expiry;gadget_handle b=button(list,5,row++*76,290,68,"",choose,(void*)(uintptr_t)i,0);Text t={{0},0};date(&t,visible_only?p->start:p->rise,1);label(b,10,7,270,20,t.s,WHITE,0,0);t=(Text){{0},0};directions(&t,p);label(b,10,31,118,20,t.s,MUTED,0,0);t=(Text){{0},0};number(&t,(p->elevation+5)/10);add(&t,"°");label(b,132,31,45,20,t.s,MINT,0,0);t=(Text){{0},0};if(p->visible)duration(&t,p->end-p->start);else add(&t,T("Not visible"));label(b,180,31,100,34,t.s,MUTED,0,0);}
 if(!row)label(list,12,40,276,50,T(forecast.fetched?"No passes in this forecast":"Waiting for predictions"),MUTED,0,1);label(body,5,310,290,20,T("Times shown in device local time"),MUTED,0,1);footer(331);return;}
 Pass*p=chosen();if(!p){int loading=!forecast.fetched&&env.online&&env.clock_ready&&!error&&(fetching||pending);label(body,8,40,284,40,T(loading?"Loading predictions...":forecast.fetched?"No visible passes forecast":"Looking for the ISS..."),WHITE,0,1);if(loading){make(body,GADGET_UI_SPINNER,130,95,40,40,0,0,0);label(body,15,170,270,60,T("Fetching passes for your location"),MUTED,0,1);}else label(body,15,110,270,80,T("Try All in the Passes tab. Visibility depends on sunlight and a dark sky."),MUTED,0,1);footer(300);return;}

 Text t={{0},0};if(p->rise>now){label(body,0,4,300,30,T(p->visible?"Next visible pass":"Next pass"),WHITE,env.german?0:GADGET_STYLE_TITLE,1);add(&t,T("In"));add(&t," ");duration(&t,(p->visible?p->start:p->rise)-now);label(body,0,36,300,40,t.s,MINT,GADGET_STYLE_VALUE,1);t=(Text){{0},0};date(&t,p->visible?p->start:p->rise,1);label(body,0,76,300,20,t.s,MUTED,0,1);compass(p);t=(Text){{0},0};add(&t,T("Rise"));add(&t," ");add(&t,direction(p->rise_az));add(&t,"  " RIGHT "  ");add(&t,T("Set"));add(&t," ");add(&t,direction(p->set_az));label(body,0,240,300,20,t.s,WHITE,0,1);label(body,0,266,150,20,T("Peak elevation"),MUTED,0,1);label(body,150,266,150,20,T(p->visible?"Visible for":"Duration"),MUTED,0,1);t=(Text){{0},0};number(&t,(p->elevation+5)/10);add(&t,"°");label(body,0,284,150,30,t.s,WHITE,GADGET_STYLE_TITLE,1);t=(Text){{0},0};duration(&t,p->visible?p->end-p->start:p->set-p->rise);label(body,150,284,150,30,t.s,WHITE,GADGET_STYLE_TITLE,1);label(body,0,315,300,20,T("Visibility depends on clear skies"),MUTED,0,1);
 }else{label(body,0,3,300,30,T("Pass in progress"),MINT,GADGET_STYLE_TITLE,1);add(&t,T("Rise"));add(&t," ");add(&t,direction(p->rise_az));add(&t,"  " RIGHT "  ");add(&t,T("Set"));add(&t," ");add(&t,direction(p->set_az));label(body,0,35,300,20,t.s,WHITE,0,1);int before=p->visible&&now<p->start,viewing=p->visible&&now>=p->start&&now<p->end;label(body,0,67,300,20,T(before?"Visible window starts in":viewing?"Visible window ends in":"Pass ends in"),MUTED,0,1);t=(Text){{0},0};duration(&t,(before?p->start:viewing?p->end:p->set)-now);label(body,0,91,300,40,t.s,MINT,GADGET_STYLE_VALUE,1);satellite();gadget_handle bar=make(body,GADGET_UI_BAR,10,188,280,12,0,0,0);ui->value(bar,100*(now-p->rise)/(p->set-p->rise));t=(Text){{0},0};add(&t,T("Rise"));add(&t," ");date(&t,p->rise,0);label(body,10,209,140,20,t.s,MUTED,0,0);t=(Text){{0},0};add(&t,T("Set"));add(&t," ");date(&t,p->set,0);label(body,165,209,130,20,t.s,MUTED,0,0);t=(Text){{0},0};add(&t,T("Peak at"));add(&t," ");date(&t,p->peak,0);label(body,10,238,280,30,t.s,WHITE,GADGET_STYLE_TITLE,0);t=(Text){{0},0};number(&t,(p->elevation+5)/10);add(&t,"° ");add(&t,T("above the horizon"));label(body,10,270,280,20,t.s,MUTED,0,0);t=(Text){{0},0};if(p->visible){add(&t,T("Visible until"));add(&t," ");date(&t,p->end,0);}else add(&t,T("Not visible - daylight or Earth shadow"));label(body,10,298,280,20,t.s,MUTED,0,0);label(body,10,315,280,20,T("Predicted times; clear skies needed"),MUTED,0,0);}
 footer(334);
}
static uint32_t generation,request_after;
static int valid(const Pass*p){return p->rise&&p->peak>=p->rise&&p->set>p->peak&&p->set-p->rise<=3600&&p->rise_az>=0&&p->rise_az<=3600&&p->set_az>=0&&p->set_az<=3600&&p->elevation>=0&&p->elevation<=900&&(!p->visible||(p->start>=p->rise&&p->end<=p->set&&p->end>p->start));}
static void insert(Forecast*f,const Pass*p){for(unsigned i=0;i<f->count;++i)if((f->passes[i].rise>p->rise?f->passes[i].rise-p->rise:p->rise-f->passes[i].rise)<5)return;if(f->count>=40)return;unsigned i=f->count++;while(i&&f->passes[i-1].rise>p->rise){f->passes[i]=f->passes[i-1];--i;}f->passes[i]=*p;}
static void path(Text*t,unsigned i,const char*key){add(t,"passes/");number(t,i);add(t,"/");add(t,key);}
static uint32_t stamp(unsigned i,const char*key){Text p={{0},0};char s[48];path(&p,i,key);return svc->json_get(p.s,s,sizeof(s))<0?0:svc->parse_time(s);}
static int32_t angle(unsigned i,const char*key){Text p={{0},0};int32_t n;path(&p,i,key);return svc->json_number(p.s,10,&n)<0?-1:n;}
static int parse(const char*data,unsigned size){if(svc->json_open(data,size))return 0;int count=svc->json_count("passes");if(count<0||count>20){svc->json_close();return 0;}char s[16];if(svc->json_get("tle_stale",s,sizeof(s))>=0&&equal(s,"true"))working.stale=1;unsigned accepted=0;
 for(unsigned i=0;i<(unsigned)count;++i){Pass p={0};p.rise=stamp(i,"rise/time");p.peak=stamp(i,"culmination/time");p.set=stamp(i,"set/time");p.rise_az=angle(i,"rise/azimuth_deg");p.set_az=angle(i,"set/azimuth_deg");p.elevation=angle(i,"culmination/elevation_deg");Text key={{0},0};path(&key,i,"visible");p.visible=svc->json_get(key.s,s,sizeof(s))>=0&&equal(s,"true");if(p.visible){p.start=stamp(i,"visible_start");p.end=stamp(i,"visible_end");}if(valid(&p)){++accepted;insert(&working,&p);}}
 svc->json_close();return count==0||accepted>0;
}
static void fail(const char*why,uint32_t retry){fetching=0;stage=0;pending=1;error=why;allowed=ticks+(retry>backoff?retry:backoff);if(backoff<1800)backoff*=2;if(backoff>1800)backoff=1800;++revision;}
static void response(int32_t code,const char*data,uint32_t size,void*user){if(closing||(uint32_t)(uintptr_t)user!=generation)return;if(code!=200){fail(code==429?"ISS rate limit - retry later":"ISS service unavailable",svc->http_retry_seconds());render();return;}if(!parse(data,size)){fail("Invalid ISS prediction data",0);render();return;}
 if(stage==1){stage=2;request_after=ticks;return;}
 for(unsigned i=0;i<forecast.count;++i)if(forecast.passes[i].rise<=now&&forecast.passes[i].set>now)insert(&working,&forecast.passes[i]);working.magic=0x49535331;working.lat=(int32_t)job_lat;working.lon=(int32_t)job_lon;working.fetched=now;forecast=working;if(host->log){Text log={{0},0};add(&log,"ISS forecast ready: ");number(&log,forecast.count);add(&log," passes; rendering current view");host->log(log.s);}svc->cache_write(&forecast,sizeof(forecast));fetching=0;stage=0;pending=0;error=0;allowed=ticks+60;next_refresh=now+1800;backoff=60;++revision;render();
}
static void load(void){forecast=(Forecast){0};Forecast saved;if(svc->cache_read(&saved,sizeof(saved))!=(int32_t)sizeof(saved)||saved.magic!=0x49535331||saved.count>40||saved.lat!=env.latitude_e6||saved.lon!=env.longitude_e6)return;for(unsigned i=0;i<saved.count;++i)if(!valid(&saved.passes[i]))return;forecast=saved;}
static void collect(void){if(!env.has_location||!env.clock_ready||!env.online)return;if(fetching&&stage!=2)return;if(fetching&&ticks<request_after)return;if(!fetching&&(!pending||ticks<allowed))return;
 Text url={{0},0};add(&url,"https://iss-api.polluxlabs.io/iss-pass?lat=");coordinate(&url,env.latitude_e6);add(&url,"&lon=");coordinate(&url,env.longitude_e6);add(&url,"&n=20&days_ahead=7&visible_only=");add(&url,fetching?"true":"false");uint32_t token=fetching?generation:generation+1;int32_t r=svc->http_get(url.s,49152,response,(void*)(uintptr_t)token);if(r==GADGET_HTTP_BUSY||r==GADGET_HTTP_RATE_LIMIT)return;if(r){fail(r==GADGET_HTTP_MEMORY?"Not enough memory for request":"ISS service unavailable",0);return;}
 if(!fetching){generation=token;working=(Forecast){0};job_lat=(uint32_t)env.latitude_e6;job_lon=(uint32_t)env.longitude_e6;stage=1;fetching=1;pending=0;error=0;++revision;}else stage=3;
}
static void tick(void*u){(void)u;if(closing)return;gadget_environment fresh={sizeof(fresh)};if(svc->environment(&fresh))return;int changed=fresh.has_location!=env.has_location||fresh.latitude_e6!=env.latitude_e6||fresh.longitude_e6!=env.longitude_e6;env=fresh;now=env.epoch;++ticks;
 if(changed){++generation;fetching=0;stage=0;error=0;load();allowed=0;pending=1;++revision;}
 if(!forecast.fetched||now<forecast.fetched||now-forecast.fetched>=1800)pending=1;collect();render();
}
static void shutdown(void){closing=1;host=0;ui=0;svc=0;root=body=location=status=0;}
__attribute__((visibility("default"))) int gadget_app_entry(int argc,char**argv){if(argc!=2)return -1;host=(const gadget_host_api*)argv[0];gadget_app_lifecycle*app=(gadget_app_lifecycle*)argv[1];if(!host||host->struct_size<offsetof(gadget_host_api,activity_read)||host->major!=1||host->minor<5||!host->services||!host->ui||!app||app->struct_size<sizeof(*app))return -1;svc=host->services;ui=host->ui;if(svc->struct_size<offsetof(gadget_services_api,config_read)||ui->struct_size<offsetof(gadget_ui_api,qr)||!svc->http_get||!svc->json_number)return -1;app->major=1;app->minor=5;app->shutdown=shutdown;
 if(ui->reserve(256))return -1;closing=fetching=passes_page=0;visible_only=1;pending=1;error=0;selected=allowed=stage=ticks=generation=request_after=0;backoff=60;revision=1;shown_revision=~0U;env=(gadget_environment){sizeof(env)};svc->environment(&env);now=env.epoch;load();if(forecast.fetched&&now>=forecast.fetched&&now-forecast.fetched<1800)pending=0;if(forecast.fetched&&now>=forecast.fetched&&now-forecast.fetched<60)allowed=60-(now-forecast.fetched);
 root=make(0,GADGET_UI_ROOT,0,0,320,480,0,0,0);ui->paint(root,WHITE,BG,100);gadget_handle header=make(root,GADGET_UI_CONTAINER,0,0,320,44,0,0,0);ui->paint(header,WHITE,0x151b2a,100);gadget_handle b=button(header,0,0,44,44,"",close_app,0,0);ui->style(b,GADGET_STYLE_BACK);label(header,46,12,225,22,T("Where is the ISS?"),WHITE,0,1);b=button(header,276,0,44,44,"",refresh,0,0);ui->style(b,GADGET_STYLE_REFRESH);location=button(root,10,46,300,30,"",open_location,0,0);ui->paint(location,MUTED,0,0);body=make(root,GADGET_UI_CONTAINER,10,78,300,354,0,0,0);tabs[0]=button(root,0,432,160,48,"Next pass",tab,0,1);tabs[1]=button(root,160,432,160,48,"Passes",tab,(void*)1,0);render();if(!host->timer_create(1000,tick,0))return -1;collect();render();return closing?-1:0;
}
