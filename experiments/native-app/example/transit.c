#include "package_common.h"
#define MAX_STOPS 8
#define MAX_CALLS 40
typedef struct {char id[513],name[97],area[17];} Stop;
typedef struct {char line[21],destination[97],trip[513],platform[25];uint32_t time,scheduled;int cancelled,live;} Departure;
typedef struct {char name[97],platform[25];uint32_t time;int cancelled;} Call;
static char config_buffer[10000];
static Stop favourites[8],found[8];static Departure departures[8];static Call calls[MAX_CALLS];
static unsigned count,found_count,departure_count,call_count,selected,generation,request_generation;
static int view,busy,pending,kind,request_kind; /* 0 board, 1 settings, 2 search, 3 trip */
static char argument[513],notice[128],query[81];
static uint32_t next_request,refresh_at,backoff,last_refresh,last_paint;
static void render(void);
static void queue(int type,const char*arg){++generation;kind=type;cp(argument,arg,sizeof argument);pending=1;notice[0]=0;dirty=1;}
static void board(void){view=0;departure_count=0;++generation;pending=0;if(count)queue(0,favourites[selected].id);dirty=1;}
static void back(void*u){(void)u;if(view==1&&settings_entry){host->request_close();return;}if(view==2){++generation;pending=0;view=1;}else if(view){board();return;}else{host->request_close();return;}dirty=1;}
static void settings(void*u){(void)u;++generation;pending=0;view=1;dirty=1;}
static int save(void){char *data=config_buffer;Writer w={data,0,sizeof config_buffer,0};put(&w,"{\"favourites\":[");for(unsigned i=0;i<count;++i){if(i)put(&w,",");put(&w,"{");field(&w,"id",favourites[i].id);put(&w,",");field(&w,"name",favourites[i].name);put(&w,",");field(&w,"area",favourites[i].area);put(&w,"}");}put(&w,"]}");return !w.failed&&svc->config_write(data,w.size)==0;}
static void remove_stop(void*u){unsigned i=(unsigned)(uintptr_t)u;if(i>=count)return;Stop old=favourites[i];for(unsigned j=i;j+1<count;++j)favourites[j]=favourites[j+1];--count;if(!save()){for(unsigned j=count;j>i;--j)favourites[j]=favourites[j-1];favourites[i]=old;++count;cp(notice,tr("Could not save stops","Haltestellen nicht gespeichert"),sizeof notice);}else{selected=0;++generation;pending=0;}dirty=1;}
static void add(void*u){unsigned i=(unsigned)(uintptr_t)u;if(i>=found_count)return;for(unsigned j=0;j<count;++j)if(eq(found[i].id,favourites[j].id)){cp(notice,tr("Stop already saved","Haltestelle bereits gespeichert"),sizeof notice);dirty=1;return;}if(count==8){cp(notice,tr("Maximum 8 favourite stops","Maximal 8 Haltestellen"),sizeof notice);dirty=1;return;}favourites[count++]=found[i];if(!save()){--count;cp(notice,tr("Could not save stops","Haltestellen nicht gespeichert"),sizeof notice);}else{view=1;notice[0]=0;}dirty=1;}
static void search_screen(void*u){(void)u;view=2;found_count=0;query[0]=notice[0]=0;dirty=1;}
static void search(void*u){(void)u;if(ui->input_get(input,query,sizeof query)<2){ui->text(body,tr("Enter at least 2 characters","Mindestens 2 Zeichen eingeben"));return;}found_count=0;queue(1,query);ui->visible(keyboard,0);dirty=1;}
static void select_stop(void*u){if(!count)return;selected=(selected+count+((intptr_t)u<0?-1:1))%count;board();}
static void choose_stop(void*u){unsigned i=(unsigned)(uintptr_t)u;if(i<count){selected=i;board();}}
static void refresh(void*u){(void)u;uint32_t now=svc->monotonic_ms();if(!count||!due(now,last_refresh+30000))return;queue(0,favourites[selected].id);}
static void details(void*u){unsigned i=(unsigned)(uintptr_t)u;if(i>=departure_count||!departures[i].trip[0])return;call_count=0;view=3;queue(2,departures[i].trip);}
static void gesture(uint32_t direction,void*u){(void)u;if(view)return;if(direction==GADGET_GESTURE_LEFT)select_stop((void*)1);else if(direction==GADGET_GESTURE_RIGHT)select_stop((void*)(intptr_t)-1);else if(direction==GADGET_GESTURE_PULL)refresh(0);}
static void render(void){
 header(tr("Transit","Abfahrten"),back);
 if(view==1){label(root,tr("Favourite stops","Gespeicherte Haltestellen"),12,50,296,24);button(root,tr("Add stop","Hinzufuegen"),12,80,296,40,search_screen,0);body=make(root,GADGET_UI_CONTAINER,"",0,128,320,302,0,0);ui->scroll(body,1);for(unsigned i=0;i<count;++i){button(body,favourites[i].name,12,i*58,240,48,choose_stop,(void*)(uintptr_t)i);button(body,"X",264,i*58,44,44,remove_stop,(void*)(uintptr_t)i);}}
 else if(view==2){button(root,tr("New","Neu"),260,0,60,44,search_screen,0);input=make(root,GADGET_UI_TEXTAREA,query,12,50,220,44,0,0);ui->input_config(input,80,0);button(root,tr("Find","Suche"),238,50,70,44,search,0);body=label(root,notice,12,100,296,30);gadget_handle list=make(root,GADGET_UI_CONTAINER,"",0,136,320,294,0,0);ui->scroll(list,1);for(unsigned i=0;i<found_count;++i)button(list,found[i].name,12,i*52,296,46,add,(void*)(uintptr_t)i);keyboard=make(root,GADGET_UI_KEYBOARD,"",0,280,320,200,0,0);ui->keyboard_attach(keyboard,input);if(found_count||busy||pending||notice[0])ui->visible(keyboard,0);}
 else if(view==3){body=make(root,GADGET_UI_CONTAINER,"",0,50,320,380,0,0);ui->scroll(body,1);for(unsigned i=0;i<call_count;++i){char t[64];svc->format_time(calls[i].time,0,t,sizeof t);if(calls[i].cancelled)cp(t,tr("Cancelled","Ausfall"),sizeof t);label(body,t,12,i*64,90,24);label(body,calls[i].name,102,i*64,206,28);label(body,calls[i].platform,102,i*64+28,206,22);}}
 else{
 button(root,tr("Stops","Orte"),260,0,60,44,settings,0);
 if(!count){label(root,tr("Add a favourite stop to begin","Zuerst eine Haltestelle hinzufuegen"),12,90,296,60);button(root,tr("Add stop","Hinzufuegen"),12,170,296,44,search_screen,0);}
 else{label(root,favourites[selected].name,12,50,296,42);button(root,"<",12,96,60,36,select_stop,(void*)(intptr_t)-1);button(root,tr("Refresh","Laden"),80,96,160,36,refresh,0);button(root,">",248,96,60,36,select_stop,(void*)1);body=make(root,GADGET_UI_CONTAINER,"",0,140,320,290,0,0);ui->scroll(body,1);ui->gestures(body,gesture,0);gadget_environment env={sizeof(env)};svc->environment(&env);
 for(unsigned i=0;i<departure_count;++i){Departure*d=&departures[i];gadget_handle row=button(body,"",12,i*82,296,76,details,(void*)(uintptr_t)i);char t[80],n[20];cp(t,d->line,sizeof t);cat(t,"  ",sizeof t);cat(t,d->destination,sizeof t);label(row,t,8,5,280,25);svc->format_time(d->time,0,t,sizeof t);if(d->cancelled)cp(t,tr("Cancelled","Ausfall"),sizeof t);else if(env.clock_ready){cat(t,"  ",sizeof t);int32_t left=(int32_t)(d->time-env.epoch);if(left<=30)cat(t,tr("Due","Jetzt"),sizeof t);else{uint_text(n,(left+59)/60);cat(t,n,sizeof t);cat(t," min",sizeof t);}}label(row,t,8,30,280,22);cp(t,d->platform,sizeof t);if(d->live)cat(t,tr("  Live","  Echtzeit"),sizeof t);if(d->time>d->scheduled+59){uint_text(n,(d->time-d->scheduled)/60);cat(t," +",sizeof t);cat(t,n,sizeof t);cat(t," min",sizeof t);}label(row,t,8,52,280,20);}
 if(!departure_count)label(body,tr("No departures loaded","Keine Abfahrten geladen"),12,20,296,44);
 }}
 if(view!=2)label(root,notice[0]?notice:busy?tr("Loading...","Lade..."):tr("Transitous - times in device timezone","Transitous - Zeit in Geraetezeitzone"),12,442,296,32);
}
static void decode_stop(Stop*s,const char*prefix,unsigned i){char p[128];path(p,prefix,i,"id");text(p,s->id,sizeof s->id);path(p,prefix,i,"name");text(p,s->name,sizeof s->name);path(p,prefix,i,"country");text(p,s->area,sizeof s->area);}
static uint32_t time_at(const char*prefix,const char*key){char p[128],s[48];cp(p,prefix,sizeof p);cat(p,"/",sizeof p);cat(p,key,sizeof p);return text(p,s,sizeof s)>0?svc->parse_time(s):0;}
static void decode_call(const char*prefix){if(call_count==MAX_CALLS)return;Call*c=&calls[call_count];char p[128];cp(p,prefix,sizeof p);cat(p,"/name",sizeof p);if(text(p,c->name,sizeof c->name)<1)return;cp(p,prefix,sizeof p);cat(p,"/track",sizeof p);text(p,c->platform,sizeof c->platform);cp(p,prefix,sizeof p);cat(p,"/cancelled",sizeof p);c->cancelled=boolean(p);c->time=time_at(prefix,"departure");if(!c->time)c->time=time_at(prefix,"arrival");if(!c->time)c->time=time_at(prefix,"scheduledDeparture");if(!c->time)c->time=time_at(prefix,"scheduledArrival");if(call_count&&eq(c->name,calls[call_count-1].name)&&c->time==calls[call_count-1].time)return;++call_count;}
static int parse(const char*json,unsigned size){if(svc->json_open(json,size)<0)return 0;int ok=0;char p[128],q[128],t[513];
 if(request_kind==1){int n=svc->json_count("");if(n<0)goto done;found_count=0;for(int i=0;i<n&&found_count<8;++i){path(p,"",i,"type");if(text(p,t,sizeof t)<0||!eq(t,"STOP"))continue;Stop*s=&found[found_count];decode_stop(s,"",i);if(s->id[0]&&s->name[0])++found_count;}ok=1;}
 else if(request_kind==0){int n=svc->json_count("stopTimes");if(n<0)goto done;departure_count=0;for(int i=0;i<n&&departure_count<8;++i){Departure*d=&departures[departure_count];zero(d,sizeof(*d));path(p,"stopTimes/",i,"place");d->time=time_at(p,"departure");d->scheduled=time_at(p,"scheduledDeparture");if(!d->time)d->time=d->scheduled;if(!d->scheduled)d->scheduled=d->time;if(!d->time)continue;
 #define GET(field,key) path(p,"stopTimes/",i,key);text(p,d->field,sizeof d->field)
 GET(line,"displayName");if(!d->line[0]){GET(line,"routeShortName");}GET(destination,"headsign");if(!d->destination[0]){GET(destination,"tripTo/name");}GET(trip,"tripId");GET(platform,"place/track");if(!d->platform[0]){GET(platform,"place/scheduledTrack");}
 #undef GET
 path(p,"stopTimes/",i,"realTime");d->live=boolean(p);path(p,"stopTimes/",i,"cancelled");d->cancelled=boolean(p);path(p,"stopTimes/",i,"tripCancelled");d->cancelled|=boolean(p);path(p,"stopTimes/",i,"place/cancelled");d->cancelled|=boolean(p);++departure_count;}
 for(unsigned i=1;i<departure_count;++i){Departure d=departures[i];unsigned j=i;while(j&&departures[j-1].time>d.time){departures[j]=departures[j-1];--j;}departures[j]=d;}ok=1;
 }else{int n=svc->json_count("legs");if(n<0)goto done;call_count=0;for(int i=0;i<n&&call_count<MAX_CALLS;++i){path(p,"legs/",i,"from");decode_call(p);path(p,"legs/",i,"intermediateStops");int m=svc->json_count(p);cat(p,"/",sizeof p);for(int j=0;j<m&&call_count<MAX_CALLS;++j){path(q,p,j,"");decode_call(q);}path(p,"legs/",i,"to");decode_call(p);}ok=1;}
 done:svc->json_close();return ok;
}
static void received(int32_t status,const char*json,uint32_t size,void*u){(void)u;busy=0;if(closing||request_generation!=generation)return;uint32_t now=svc->monotonic_ms();if(status==200&&parse(json,size)){notice[0]=0;backoff=60000;refresh_at=now+120000;if(request_kind==0)last_refresh=now;next_request=now+2000;}else{cp(notice,status==429?tr("Transitous rate limit","Transitous-Anfragelimit"):status==200?tr("Invalid transit response","Ungueltige Verkehrsdaten"):tr("Offline / request failed; cached times","Offline / Anfrage fehlgeschlagen; Cache"),sizeof notice);uint32_t delay=svc->http_retry_seconds();if(delay>900)delay=900;delay*=1000;if(delay<backoff)delay=backoff;next_request=refresh_at=now+delay;if(backoff<900000)backoff=backoff>450000?900000:backoff*2;}dirty=1;}
static void tick(void*u){(void)u;if(closing)return;uint32_t now=svc->monotonic_ms();if(view==0&&count&&!pending&&!busy&&due(now,refresh_at))queue(0,favourites[selected].id);
 if(pending&&!busy&&due(now,next_request)){char url[2048],encoded[1540];if(encode(encoded,sizeof encoded,argument)<0){pending=0;return;}cp(url,"https://api.transitous.org/api/",sizeof url);cat(url,kind==1?"v1/geocode?type=STOP&numResults=8&text=":kind==0?"v6/stoptimes?n=8&withAlerts=false&stopId=":"v6/trip?detailedLegs=false&tripId=",sizeof url);cat(url,encoded,sizeof url);request_generation=generation;request_kind=kind;int r=svc->http_get(url,196608,received,0);if(r==0){busy=1;pending=0;dirty=1;}else if(r!=GADGET_HTTP_BUSY&&r!=GADGET_HTTP_RATE_LIMIT){pending=0;received(r,0,0,0);}}
 if(view==0&&now-last_paint>=30000){last_paint=now;dirty=1;}
 if(dirty&&ui->interaction_active(body)<=0){dirty=0;render();}}
static void shutdown(void){closing=1;++generation;root=body=input=keyboard=0;}
__attribute__((visibility("default"))) int gadget_app_entry(int argc,char**argv){gadget_app_lifecycle*life;if(init(argc,argv,&life))return -1;life->shutdown=shutdown;count=found_count=departure_count=call_count=selected=generation=0;busy=pending=0;view=settings_entry?1:0;notice[0]=query[0]=0;backoff=60000;next_request=refresh_at=svc->monotonic_ms();last_refresh=next_request-30000;
 char *data=config_buffer;int n=svc->config_read(data,sizeof config_buffer);if(n>0&&svc->json_open(data,n)==0){int size=svc->json_count("favourites");for(int i=0;i<size&&count<8;++i){Stop*s=&favourites[count];decode_stop(s,"favourites/",i);char p[128];path(p,"favourites/",i,"area");text(p,s->area,sizeof s->area);int duplicate=0;for(unsigned j=0;j<count;++j)if(eq(s->id,favourites[j].id))duplicate=1;if(s->id[0]&&s->name[0]&&!duplicate)++count;}svc->json_close();}
 root=ui->create(0,GADGET_UI_ROOT,"",0,0);if(!root||ui->reserve(160)<0||!host->timer_create(250,tick,0))return -1;render();dirty=0;return 0;}
