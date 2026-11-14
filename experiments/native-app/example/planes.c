#include "package_common.h"
#include "package_status.h"
#include "package_visual.h"
#include "package_text.h"
#include "planes_cache.h"
#define PI 3.14159265358979323846
typedef struct {char id[17],callsign[25],registration[25],type[25],description[81],country[49],squawk[9],category[9];double lat,lon,alt,speed,heading,vertical,distance;int ground,military,heading_valid;} Plane;
typedef struct {double lat,lon,alt;} Point;
typedef PlanesAirport Airport;
static Plane planes[20],fresh[20];static unsigned count,selected,fresh_count,generation,request_generation,provider,request_provider;
typedef struct {char id[17];Point points[20];unsigned count;} AircraftTrail;
static AircraftTrail trails[20];
static Point trail[20];static unsigned trail_count;
typedef struct {char call[25],route[80],origin[97],destination[97];uint32_t expires;Airport airports[2];unsigned airport_count;} Route;
static Route routes[64];static unsigned route_slot,route_budget;
static char detail_id[17];static uint32_t last_paint;static double trail_latitude,trail_longitude;
static PlanesAirportCache airports;
static char selected_id[17],route_call[25],route[80],origin[97],destination[97],notice[160],edit_value[40];
static double latitude,longitude,range_km,edit_lat,edit_lon,edit_range;
static int metric,hide_ground,override_location,edit_metric,edit_hide,edit_override,view,edit_field,busy,request_kind,configured;
static uint32_t next_request,refresh_at,last_refresh,route_due;
static int guess_pending,range_open;
static char location_notice[160];
static const char*providers[]={"adsb.lol","airplanes.live","adsb.fi","OpenSky"};
static void render(void);
static void received(int32_t,const char*,uint32_t,void*);
static double absval(double a){return a<0?-a:a;}
static double clamp(double v,double low,double high){return v<low?low:v>high?high:v;}
static double sinval(double n){return svc->math->unary(0,n);}
static double cosval(double n){return svc->math->unary(1,n);}
static double distance(double a,double b,double c,double d){double dl=(c-a)*PI/180,dp=(d-b)*PI/180;double s=sinval(dl/2),t=sinval(dp/2);double h=clamp(s*s+cosval(a*PI/180)*cosval(c*PI/180)*t*t,0,1);return 12742*svc->math->binary(4,svc->math->unary(2,h),svc->math->unary(2,1-h));}
static double bearing(double a,double b){double p=latitude*PI/180,q=a*PI/180,d=(b-longitude)*PI/180;return svc->math->binary(4,sinval(d)*cosval(q),cosval(p)*sinval(q)-sinval(p)*cosval(q)*cosval(d));}
static int trail_segment(Point a,Point b,int*x,int*y,int*u,int*v){
 double ar=distance(latitude,longitude,a.lat,a.lon),br=distance(latitude,longitude,b.lat,b.lon),aa=bearing(a.lat,a.lon),ba=bearing(b.lat,b.lon);
 double ax=ar*sinval(aa),ay=ar*cosval(aa),bx=br*sinval(ba),by=br*cosval(ba),dx=bx-ax,dy=by-ay,A=dx*dx+dy*dy,B=2*(ax*dx+ay*dy),C=ax*ax+ay*ay-range_km*range_km;
 if(A<1e-14)return 0;double d=B*B-4*A*C;if(d<0)return 0;d=svc->math->unary(2,d);double start=clamp((-B-d)/(2*A),0,1),end=clamp((-B+d)/(2*A),0,1);if(start>=end)return 0;
 *x=150+(int)((ax+start*dx)*112/range_km);*y=128-(int)((ay+start*dy)*112/range_km);*u=150+(int)((ax+end*dx)*112/range_km);*v=128-(int)((ay+end*dy)*112/range_km);return 1;
}
static int position(double a,double b,int*x,int*y){double r=distance(latitude,longitude,a,b)/range_km;if(r>1.05)return 0;double angle=bearing(a,b);*x=150+(int)(r*sinval(angle)*112);*y=128-(int)(r*cosval(angle)*112);return *x>=10&&*x<=290&&*y>=10&&*y<=246;}
static unsigned altitude_color(double m){return m<0?0x9aa5bd:m<2000?0xffc76b:m<4000?0x83e377:m<6000?0x56d9ed:m<9000?0x6da9ff:0xcf94ff;}
static void number(char*s,double v,unsigned decimals){if(svc->format_number(v,decimals,s,40)<0)cp(s,"--",40);}
static int decimal(const char*s,double*out){int sign=1,point=0,digits=0;double n=0,scale=1;if(*s=='-'){sign=-1;++s;}else if(*s=='+')++s;for(;*s;++s){if(*s=='.'&&!point){point=1;continue;}if(*s<'0'||*s>'9')return 0;digits=1;if(point){scale*=10;n+=(*s-'0')/scale;}else n=n*10+(*s-'0');if(n>1000000)return 0;}*out=sign*n;return digits;}
static int valid_airports(void){if(airports.magic!=0x504c4131||airports.count>64||airports.lat!=airports.lat||airports.lon!=airports.lon||airports.range!=airports.range||absval(airports.lat)>90||absval(airports.lon)>180||airports.range<0.1||airports.range>20000)return 0;for(unsigned i=0;i<airports.count;++i){Airport*a=&airports.airports[i];if(a->lat!=a->lat||a->lon!=a->lon||absval(a->lat)>90||absval(a->lon)>180||a->code[8]||a->name[96])return 0;}return 1;}
static void selected_route(void){route[0]=origin[0]=destination[0]=0;if(selected>=count)return;for(unsigned i=0;i<64;++i)if(eq(routes[i].call,planes[selected].callsign)){cp(route,routes[i].route,sizeof route);cp(origin,routes[i].origin,sizeof origin);cp(destination,routes[i].destination,sizeof destination);return;}}
static void selected_trail(void){trail_count=0;for(unsigned i=0;i<20;++i)if(eq(trails[i].id,selected_id)){trail_count=trails[i].count;for(unsigned j=0;j<trail_count;++j)trail[j]=trails[i].points[j];return;}}
static void record_trails(void){
 if(trail_latitude!=latitude||trail_longitude!=longitude){zero(trails,sizeof trails);trail_latitude=latitude;trail_longitude=longitude;}
 int present[20];zero(present,sizeof present);
 for(unsigned i=0;i<count;++i)for(unsigned j=0;j<20;++j)if(eq(planes[i].id,trails[j].id))present[j]=1;
 for(unsigned i=0;i<count;++i){Plane*p=&planes[i];unsigned slot=20;for(unsigned j=0;j<20;++j)if(eq(p->id,trails[j].id)){slot=j;break;}
  if(slot==20){for(unsigned j=0;j<20;++j)if(!trails[j].id[0]){slot=j;break;}if(slot==20)for(unsigned j=0;j<20;++j)if(!present[j]){slot=j;break;}if(slot==20)continue;zero(&trails[slot],sizeof trails[slot]);cp(trails[slot].id,p->id,sizeof trails[slot].id);present[slot]=1;}
  AircraftTrail*t=&trails[slot];if(t->count&&t->points[t->count-1].lat==p->lat&&t->points[t->count-1].lon==p->lon){t->points[t->count-1].alt=p->alt;continue;}
  if(t->count==20){for(unsigned j=1;j<20;++j)t->points[j-1]=t->points[j];--t->count;}t->points[t->count++]=(Point){p->lat,p->lon,p->alt};
 }selected_trail();
}
static void reset_area(void){++generation;count=trail_count=0;selected_id[0]=route_call[0]=route[0]=origin[0]=destination[0]=0;provider=0;refresh_at=svc->monotonic_ms();route_due=0;
 if(trail_latitude!=latitude||trail_longitude!=longitude){zero(trails,sizeof trails);trail_latitude=latitude;trail_longitude=longitude;}
 if(airports.magic!=0x504c4131||distance(airports.lat,airports.lon,latitude,longitude)>0.002||absval(airports.range-range_km)>0.0001){zero(&airports,sizeof airports);airports.magic=0x504c4131;airports.lat=latitude;airports.lon=longitude;airports.range=range_km;svc->cache_write(&airports,sizeof airports);}}
static void resolve_location(void){configured=1;if(!override_location){gadget_environment env={sizeof(env)};svc->environment(&env);if(!env.has_location){configured=0;return;}double a=env.latitude_e6/1000000.0,b=env.longitude_e6/1000000.0;if(a!=latitude||b!=longitude){latitude=a;longitude=b;reset_area();}}}
static int changed(void){return edit_lat!=latitude||edit_lon!=longitude||edit_range!=range_km||edit_metric!=metric||edit_hide!=hide_ground||edit_override!=override_location;}
static void cancel_guess(void){if(guess_pending||(busy&&request_kind==2))++generation;guess_pending=0;}
static void back(void*u){(void)u;if(range_open){range_open=0;dirty=1;return;}if(view==3)view=edit_field==2?1:5;else if(view==5){cancel_guess();view=1;}else if(view==1){if(changed()){view=4;dirty=1;return;}if(settings_entry){host->request_close();return;}view=0;}else if(view)view=0;else{host->request_close();return;}dirty=1;}
static void discard(void*u){(void)u;cancel_guess();notice[0]=0;view=0;if(settings_entry)host->request_close();else dirty=1;}
static void keep_editing(void*u){(void)u;view=1;dirty=1;}
static void settings(void*u){(void)u;range_open=0;++generation;guess_pending=0;notice[0]=location_notice[0]=0;edit_lat=latitude;edit_lon=longitude;edit_range=range_km;edit_metric=metric;edit_hide=hide_ground;edit_override=override_location;view=1;dirty=1;}
static void edit(void*u){cancel_guess();notice[0]=0;edit_field=(int)(uintptr_t)u;number(edit_value,edit_field==0?edit_lat:edit_field==1?edit_lon:edit_range,edit_field==2?1:6);view=3;dirty=1;}
static void edit_save(void*u){
 (void)u;char s[40];double n;int size=ui->input_get(input,s,sizeof s);char *value=s;
 if(size>=0){while(*value==' ')++value;unsigned n=len(value);while(n&&value[n-1]==' ')value[--n]=0;for(unsigned i=0;i<n;++i)if(value[i]==',')value[i]='.';}
 if(size<0||!decimal(value,&n)||n<(edit_field==0?-90:edit_field==1?-180:0.1)||n>(edit_field==0?90:edit_field==1?180:20000)){
  if(size>=0)cp(edit_value,s,sizeof edit_value);cp(notice,tr("Enter a valid number","Gueltige Zahl eingeben"),sizeof notice);dirty=1;return;
 }
 if(edit_field==0)edit_lat=n;else if(edit_field==1)edit_lon=n;else edit_range=n;
 notice[0]=0;view=edit_field==2?1:5;dirty=1;
}
static void location(void*u){(void)u;view=5;dirty=1;}
static void choice(void*u){
 unsigned p=(unsigned)(uintptr_t)u;
 if(p<2)edit_metric=p==0;else if(p<4)edit_hide=p==3;else{cancel_guess();edit_override=p==5;location_notice[0]=0;}
 dirty=1;
}
static void range_choice(void*u){edit_range=(unsigned)(uintptr_t)u;dirty=1;}
static void guess_location(void*u){
 (void)u;if(guess_pending||(busy&&request_kind==2))return;
 ++generation;guess_pending=1;next_request=svc->monotonic_ms();
 cp(location_notice,tr("Finding your approximate location...","Ungefaehren Standort ermitteln..."),sizeof location_notice);dirty=1;
}
static void choice_button(const char*title,int x,int y,int w,gadget_callback cb,unsigned value,int selected){
 gadget_handle b=button(root,title,x,y,w,44,cb,(void*)(uintptr_t)value);ui->selected(b,selected);
}
static void save(void*u){(void)u;cancel_guess();char data[512],n[40];Writer w={data,0,sizeof data,0};put(&w,"{");number(n,edit_lat,6);field(&w,"center_latitude",n);put(&w,",");number(n,edit_lon,6);field(&w,"center_longitude",n);put(&w,",");number(n,edit_range,1);field(&w,"range_km",n);put(&w,edit_metric?",\"metric\":true":",\"metric\":false");put(&w,edit_hide?",\"hide_ground\":true":",\"hide_ground\":false");put(&w,edit_override?",\"override_location\":true}":",\"override_location\":false}");if(w.failed||svc->config_write(data,w.size)<0){cp(notice,tr("Could not save preferences","Einstellungen nicht gespeichert"),sizeof notice);dirty=1;return;}latitude=edit_lat;longitude=edit_lon;range_km=edit_range;metric=edit_metric;hide_ground=edit_hide;override_location=edit_override;reset_area();resolve_location();notice[0]=0;if(settings_entry){host->request_close();return;}view=0;dirty=1;}
static void select_plane(void*u){unsigned i=(unsigned)(uintptr_t)u;if(i>=count)return;selected=i;cp(selected_id,planes[i].id,sizeof selected_id);selected_trail();selected_route();dirty=1;}
static void details(void*u){(void)u;if(selected_id[0]){cp(detail_id,selected_id,sizeof detail_id);view=2;dirty=1;}}
static void refresh(void*u){(void)u;uint32_t now=svc->monotonic_ms();if(due(now,last_refresh+15000)){provider=0;refresh_at=now;}}
static void zoom(void*u){(void)u;range_open=!range_open;dirty=1;}
static void radar_range(void*u){range_km=(unsigned)(uintptr_t)u;range_open=0;reset_area();dirty=1;}
static void navigate(void*u){if(count)select_plane((void*)(uintptr_t)((selected+count+((intptr_t)u<0?-1:1))%count));}
static const char*direction(Plane*p){static const char*en[]={"north","northeast","east","southeast","south","southwest","west","northwest"};static const char*de[]={"Norden","Nordosten","Osten","Suedosten","Sueden","Suedwesten","Westen","Nordwesten"};double a=bearing(p->lat,p->lon)*180/PI;if(a<0)a+=360;return german?de[((unsigned)(a+22.5)/45)%8]:en[((unsigned)(a+22.5)/45)%8];}
static const char*airline(Plane*p){static const char*codes[]={"SWR","CFG","DLH","BAW","KLM","AFR","EZY","RYR","UAE","QTR","DAL","UAL","AAL","EIN"};static const char*names[]={"SWISS","Condor","Lufthansa","British Airways","KLM","Air France","easyJet","Ryanair","Emirates","Qatar","Delta","United","American","Aer Lingus"};for(unsigned i=0;i<14;++i)if(p->callsign[0]==codes[i][0]&&p->callsign[1]==codes[i][1]&&p->callsign[2]==codes[i][2])return names[i];return tr("Aircraft","Flugzeug");}
static const char*aircraft_type(Plane*p){if(p->description[0])return p->description;static const char*codes[]={"BCS3","A320","A20N","A21N","A321","A339","B738","B77W","E190","E195","E290","E295"};static const char*names[]={"Airbus A220-300","Airbus A320","Airbus A320neo","Airbus A321neo","Airbus A321","Airbus A330-900","Boeing 737-800","Boeing 777-300ER","Embraer E190","Embraer E195","Embraer E190-E2","Embraer E195-E2"};for(unsigned i=0;i<12;++i)if(eq(p->type,codes[i]))return names[i];return p->type[0]?p->type:tr("Unknown type","Unbekannter Typ");}
static void altitude(char*out,Plane*p){if(p->ground){cp(out,tr("On ground","Am Boden"),160);return;}if(p->alt<0){cp(out,"--",160);return;}char n[40];number(n,metric?p->alt:p->alt*3.28084,0);cp(out,n,160);cat(out,metric?" m":" ft",160);}
static void stat(gadget_handle parent,const char*title,const char*value,int x,int y){caption(parent,title,x,y,136,20,0xaab8d2,0);caption(parent,value,x,y+24,136,32,0xffffff,1);}
static int rendered_view=-1;
static void render(void){int scroll=view==2&&rendered_view==2&&body?ui->scroll_y(body,-1):0;rendered_view=view;header(view==1?tr("Planes settings","Flugzeug-Einstellungen"):view==5?tr("Radar location","Radar-Standort"):tr("Nearby aircraft","Flugzeuge in der Naehe"),back);header_status(root,view==0?198:286,busy);
 if(view==4){label(root,tr("Save your changes?","Aenderungen speichern?"),12,100,296,60);button(root,tr("Save","Speichern"),12,180,296,44,save,0);button(root,tr("Discard","Verwerfen"),12,240,296,44,discard,0);button(root,tr("Keep editing","Weiter bearbeiten"),12,300,296,44,keep_editing,0);return;}
 if(view==3){const char*title=edit_field==0?tr("Latitude","Breitengrad"):edit_field==1?tr("Longitude","Laengengrad"):tr("Range (km)","Radius (km)");label(root,title,12,64,296,30);input=make(root,GADGET_UI_TEXTAREA,edit_value,12,110,296,48,0,0);ui->input_config(input,32,0);button(root,tr("Save","Speichern"),12,180,296,44,edit_save,0);keyboard=make(root,GADGET_UI_KEYBOARD,"",0,280,320,200,0,0);ui->keyboard_attach(keyboard,input);label(root,notice,12,236,296,30);return;}
 if(view==1){
  label(root,tr("Location","Standort"),12,54,296,22);
  button(root,edit_override?tr("Custom location  >","Eigener Standort  >"):tr("Device location  >","Geraetestandort  >"),12,80,296,44,location,0);
  label(root,tr("Units","Einheiten"),12,140,296,22);
  choice_button(tr("Metric","Metrisch"),12,166,144,choice,0,edit_metric);
  choice_button(tr("Aviation","Luftfahrt"),164,166,144,choice,1,!edit_metric);
  label(root,tr("Aircraft on ground","Flugzeuge am Boden"),12,226,296,22);
  ui->wrap(label(root,tr("The nearby radar shows airborne aircraft only.","Das Radar zeigt nur Flugzeuge in der Luft."),12,252,296,44));
  label(root,tr("Radar range (km)","Radar-Radius (km)"),12,312,296,22);
  const unsigned ranges[]={5,10,25,50};for(unsigned i=0;i<4;++i){char n[16];uint_text(n,ranges[i]);choice_button(n,12+(int)i*76,338,68,range_choice,ranges[i],edit_range==ranges[i]);}
  char n[40],line[64];number(n,edit_range,1);cp(line,n,sizeof line);cat(line," km...",sizeof line);
  button(root,line,12,396,144,40,edit,(void*)2);gadget_handle done=button(root,tr("Save","Speichern"),164,396,144,40,save,0);ui->selected(done,1);
  label(root,notice,12,444,296,28);return;
 }
 if(view==5){
  choice_button(tr("Device location","Geraetestandort"),12,56,144,choice,4,!edit_override);
  choice_button(tr("Custom","Eigener Ort"),164,56,144,choice,5,edit_override);
  gadget_environment env={sizeof env};svc->environment(&env);
  gadget_handle help=label(root,edit_override?tr("Only used by Planes. Review the approximate IP location before saving.","Nur fuer Planes. Ungefaehren IP-Standort vor dem Speichern pruefen."):env.has_location?tr("Uses the shared device location. IP lookup creates a custom location for Planes.","Nutzt den Geraetestandort. IP-Suche erstellt einen eigenen Ort fuer Planes."):tr("No device location set. Guess from IP or enter custom coordinates.","Kein Geraetestandort. IP-Suche nutzen oder Koordinaten eingeben."),12,112,296,56);ui->wrap(help);
  button(root,guess_pending||(busy&&request_kind==2)?tr("Locating...","Suche laeuft..."):tr("Guess from IP","Aus IP ermitteln"),12,180,296,48,guess_location,0);
  gadget_handle result=label(root,location_notice,12,240,296,48);ui->wrap(result);
  double a=edit_override?edit_lat:env.latitude_e6/1000000.0,b=edit_override?edit_lon:env.longitude_e6/1000000.0;char n[40],line[80];
  number(n,a,5);cp(line,tr("Latitude: ","Breite: "),sizeof line);cat(line,!edit_override&&!env.has_location?"--":n,sizeof line);
  if(edit_override)button(root,line,12,300,296,44,edit,0);else label(root,line,12,300,296,44);
  number(n,b,5);cp(line,tr("Longitude: ","Laenge: "),sizeof line);cat(line,!edit_override&&!env.has_location?"--":n,sizeof line);
  if(edit_override)button(root,line,12,354,296,44,edit,(void*)1);else label(root,line,12,354,296,44);
  button(root,tr("Done","Fertig"),12,422,296,44,back,0);return;
 }
 ui->paint(root,0xffffff,0x1e2030,100);
 if(view==2){
  Plane*p=0;for(unsigned i=0;i<count;++i)if(eq(detail_id,planes[i].id))p=&planes[i];
  if(!p){label(root,tr("Aircraft no longer in view","Flugzeug nicht mehr sichtbar"),12,70,296,60);button(root,tr("Close","Schliessen"),12,430,296,40,back,0);return;}
  body=make(root,GADGET_UI_CONTAINER,"",10,52,300,366,0,0);ui->scroll(body,1);
  caption(body,airline(p),12,6,276,20,0xaab8d2,0);caption(body,p->callsign,12,30,276,32,0xffffff,1);
  caption(body,aircraft_type(p),12,66,276,22,0xaab8d2,0);
  const char*detail_route="",*detail_origin="",*detail_destination="";for(unsigned i=0;i<64;++i)if(eq(routes[i].call,p->callsign)){detail_route=routes[i].route;detail_origin=routes[i].origin;detail_destination=routes[i].destination;break;}
  caption(body,detail_route[0]?detail_route:tr("Route unavailable / loading","Route nicht verfuegbar / laedt"),12,104,276,32,0x63e6be,detail_route[0]!=0);
  ui->wrap(caption(body,detail_origin,12,144,132,44,0xaab8d2,0));ui->wrap(caption(body,detail_destination,156,144,132,44,0xaab8d2,0));
  char row[160],n[40];altitude(row,p);stat(body,tr("Altitude","Hoehe"),row,12,210);
  number(n,metric?p->speed*3.6:p->speed*1.943844,0);cp(row,n,sizeof row);cat(row,metric?" km/h":" kt",sizeof row);stat(body,tr("Speed","Tempo"),row,156,210);
  number(n,metric?p->distance:p->distance/1.852,1);cp(row,n,sizeof row);cat(row,metric?" km":" nm",sizeof row);stat(body,tr("Distance","Entfernung"),row,12,284);stat(body,tr("Direction","Richtung"),direction(p),156,284);
  cp(row,p->registration,sizeof row);cat(row,"   Squawk: ",sizeof row);cat(row,p->squawk,sizeof row);caption(body,row,12,358,276,24,0xaab8d2,0);
  number(n,metric?p->vertical:p->vertical/0.00508,1);cp(row,tr("Vertical speed: ","Steigrate: "),sizeof row);cat(row,n,sizeof row);cat(row,metric?" m/s":" ft/min",sizeof row);caption(body,row,12,390,276,24,0xaab8d2,0);cp(row,p->country,sizeof row);if(p->military)cat(row,tr(" / Military"," / Militaer"),sizeof row);caption(body,row,12,422,276,24,0xaab8d2,0);cp(row,"ICAO: ",sizeof row);cat(row,p->id,sizeof row);caption(body,row,12,454,276,24,0xaab8d2,0);if(notice[0])caption(body,tr("Saved data - update failed","Gespeicherte Daten - Update fehlgeschlagen"),12,486,276,40,0xffc66d,0);
  if(scroll>0)ui->scroll_y(body,scroll);button(root,tr("Close","Schliessen"),12,430,296,40,back,0);return;
 }
 button(root,tr("Setup","Setup"),222,0,54,44,settings,0);ui->style(button(root,"",276,0,44,44,refresh,0),GADGET_STYLE_REFRESH);
 char row[160],n[40];uint_text(row,count);cat(row,tr(" airborne nearby"," Flugzeuge in der Luft"),sizeof row);caption(root,row,10,54,210,22,0xaab8d2,0);
 number(n,metric?range_km:range_km/1.852,1);cp(row,n,sizeof row);cat(row,metric?" km":" nm",sizeof row);button(root,row,224,44,86,38,zoom,0);
 body=make(root,GADGET_UI_CONTAINER,"",10,74,300,262,0,0);gadget_handle layer=make(body,GADGET_UI_CONTAINER,"",0,0,300,262,0,0);gadget_draw_command commands[64];unsigned nc=0;
 commands[nc++]=(gadget_draw_command){GADGET_DRAW_CIRCLE,150,128,112,0,1,0x526480};commands[nc++]=(gadget_draw_command){GADGET_DRAW_CIRCLE,150,128,56,0,1,0x526480};commands[nc++]=(gadget_draw_command){GADGET_DRAW_LINE,38,128,262,128,1,0x35445e};commands[nc++]=(gadget_draw_command){GADGET_DRAW_LINE,150,16,150,240,1,0x35445e};commands[nc++]=(gadget_draw_command){GADGET_DRAW_CIRCLE,150,128,3,0,3,0xffffff};
 /* 5 rings/axes + 19 trail + 80 aircraft + selection + 5 legend <= 110 draw commands.
  * Airports use tiny rounded widgets, so all 64 landmarks fit without stealing trail budget. */
 for(unsigned i=1;i<trail_count;++i){int x,y,a,b;if(trail_segment(trail[i-1],trail[i],&x,&y,&a,&b)){unsigned c=altitude_color(trail[i].alt),fade=60+170*i/(trail_count-1),bg=0x1e2030;unsigned r=(((c>>16)&255)*fade+((bg>>16)&255)*(255-fade))/255,g=(((c>>8)&255)*fade+((bg>>8)&255)*(255-fade))/255,bl=((c&255)*fade+(bg&255)*(255-fade))/255;commands[nc++]=(gadget_draw_command){GADGET_DRAW_LINE,x,y,a,b,2,(r<<16)|(g<<8)|bl};}}
 int labels[64][4];unsigned label_count=0;
 for(unsigned i=0;i<airports.count;++i){int x,y;if(distance(latitude,longitude,airports.airports[i].lat,airports.airports[i].lon)>range_km||!position(airports.airports[i].lat,airports.airports[i].lon,&x,&y))continue;
  gadget_handle dot=make(layer,GADGET_UI_CONTAINER,"",x-2,y-2,4,4,0,0);ui->style(dot,GADGET_STYLE_CARD);ui->style(dot,GADGET_STYLE_DECORATIVE);ui->paint(dot,0x65748a,0x65748a,100);
  int w=len(airports.airports[i].code)*11,left=x+6,top=y-9,overlap=0;if(left+w>262)left=x-6-w;
  for(unsigned j=0;j<label_count;++j)if(left<=labels[j][2]+3&&left+w+3>=labels[j][0]&&top<=labels[j][3]+3&&top+21>=labels[j][1])overlap=1;
  if(!overlap){labels[label_count][0]=left;labels[label_count][1]=top;labels[label_count][2]=left+w;labels[label_count++][3]=top+18;caption(layer,airports.airports[i].code,left,top,w,18,0x65748a,0);}
 }
 gadget_handle trail_layer=make(body,GADGET_UI_CONTAINER,"",0,0,300,262,0,0);ui->style(trail_layer,GADGET_STYLE_DECORATIVE);ui->draw(trail_layer,commands+5,nc-5);nc=5;
 layer=make(body,GADGET_UI_CONTAINER,"",0,0,300,262,0,0);
 for(unsigned i=0;i<count;++i){int x,y;if(!position(planes[i].lat,planes[i].lon,&x,&y))continue;unsigned c=altitude_color(planes[i].alt);gadget_handle marker=make(layer,GADGET_UI_CONTAINER,"",x-10,y-10,20,20,0,0);gadget_draw_command strokes[4];unsigned ns=0;
  if(planes[i].heading_valid){const int coords[4][4]={{0,-7,0,7},{-7,2,0,-2},{0,-2,7,2},{-3,7,3,7}};double h=planes[i].heading*PI/180,sn=sinval(h),cs=cosval(h);for(unsigned j=0;j<4;++j){const int*v=coords[j];strokes[ns++]=(gadget_draw_command){GADGET_DRAW_LINE,10+(int)(v[0]*cs-v[1]*sn),10+(int)(v[0]*sn+v[1]*cs),10+(int)(v[2]*cs-v[3]*sn),10+(int)(v[2]*sn+v[3]*cs),2,c};}}
  else strokes[ns++]=(gadget_draw_command){GADGET_DRAW_CIRCLE,10,10,3,0,2,c};ui->draw(marker,strokes,ns);
  gadget_handle hit=button(layer,"",x-12,y-12,24,24,select_plane,(void*)(uintptr_t)i);ui->paint(hit,0xffffff,0,0);
  if(eq(planes[i].id,selected_id))commands[nc++]=(gadget_draw_command){GADGET_DRAW_CIRCLE,x,y,12,0,1,0x63e6be};
 }
 for(unsigned i=0;i<5;++i)commands[nc++]=(gadget_draw_command){GADGET_DRAW_LINE,2+(int)i*12,235,12+(int)i*12,235,3,altitude_color(i*2500)};
 ui->draw(body,commands,nc);
 caption(body,"N",144,0,16,20,0xaab8d2,0);caption(body,"S",144,242,16,20,0xaab8d2,0);caption(body,"W",18,119,18,20,0xaab8d2,0);caption(body,"E",267,119,18,20,0xaab8d2,0);caption(body,tr("You","Du"),137,137,40,20,0xaab8d2,0);caption(body,tr("Low - High","Tief - Hoch"),0,243,100,18,0xaab8d2,0);caption(body,tr("North up","Norden oben"),209,238,90,20,0xaab8d2,0);
 gadget_handle info=card(root,10,338,300,108,0x293448,details,0);ui->style(info,GADGET_STYLE_OUTLINED);
 if(selected<count&&selected_id[0]){Plane*p=&planes[selected];cp(row,tr("Look ","Blick nach "),sizeof row);cat(row,direction(p),sizeof row);caption(info,row,10,7,280,28,0x63e6be,1);
  number(n,metric?p->distance:p->distance/1.852,1);cp(row,n,sizeof row);cat(row,metric?" km":" nm",sizeof row);if(p->heading_valid&&p->speed>=5){double motion=cosval(p->heading*PI/180-bearing(p->lat,p->lon));cat(row,motion<-.35?tr(" away - Approaching"," - kommt naeher"):motion>.35?tr(" away - Moving away"," - entfernt sich"):tr(" away - Passing by"," - fliegt vorbei"),sizeof row);}caption(info,row,10,35,280,18,0xffffff,0);
  cp(row,airline(p),sizeof row);cat(row," - ",sizeof row);cat(row,p->callsign,sizeof row);caption(info,row,10,route[0]?52:61,280,18,0xffffff,0);
  if(route[0])caption(info,route,10,72,280,18,0x63e6be,0);
  cp(row,aircraft_type(p),sizeof row);cat(row," - ",sizeof row);char alt[160];altitude(alt,p);cat(row,alt,sizeof row);caption(info,row,10,route[0]?88:83,280,18,0xaab8d2,0);
 }else{caption(info,!configured?tr("Set your location","Standort einstellen"):busy?tr("Looking for aircraft...","Suche Flugzeuge..."):tr("No aircraft nearby","Keine Flugzeuge in der Naehe"),10,10,280,32,0x63e6be,1);ui->wrap(caption(info,configured?tr("Try a larger radius. Aircraft appear here.","Groesseren Radius versuchen."):tr("Choose a location in Setup.","Standort im Setup waehlen."),10,50,280,46,0xaab8d2,0));}
 if(notice[0])cp(row,count?tr("Saved data - retrying","Cache - neuer Versuch"):tr("Update failed - retry","Update fehlgeschlagen"),sizeof row);else if(busy)cp(row,tr("Updating...","Aktualisieren..."),sizeof row);else{cp(row,tr("Updated ","Aktualisiert vor "),sizeof row);uint_text(n,(svc->monotonic_ms()-last_refresh)/1000);cat(row,n,sizeof row);cat(row,tr("s ago"," s"),sizeof row);}caption(root,row,10,456,152,20,0xaab8d2,0);
 button(root,"<",170,448,44,32,navigate,(void*)(intptr_t)-1);uint_text(row,count&&selected<count?selected+1:0);cat(row," / ",sizeof row);uint_text(n,count);cat(row,n,sizeof row);caption(root,row,216,456,48,20,0xaab8d2,0);button(root,">",266,448,44,32,navigate,(void*)1);
 if(range_open){gadget_handle menu=card(root,210,84,100,200,0x293448,0,0);const unsigned ranges[]={5,10,25,50};for(unsigned i=0;i<4;++i){number(n,metric?ranges[i]:ranges[i]/1.852,1);cp(row,n,sizeof row);cat(row,metric?" km":" nm",sizeof row);gadget_handle b=button(menu,row,4,4+i*48,92,44,radar_range,(void*)(uintptr_t)ranges[i]);ui->selected(b,range_km==ranges[i]);}}

}

static double real_at(const char*prefix,unsigned i,const char*key,double fallback){char p[128];double n;path(p,prefix,i,key);return svc->json_real(p,&n)==0?n:fallback;}
static int decode(const char*json,unsigned size){if(svc->json_open(json,size)<0)return 0;const char*prefix="ac/";int n=svc->json_count("ac"),sky=0;if(n<0){prefix="aircraft/";n=svc->json_count("aircraft");}if(n<0){prefix="states/";n=svc->json_count("states");sky=1;}if(n<0){svc->json_close();return 0;}fresh_count=0;
 for(int i=0;i<n;++i){Plane p;zero(&p,sizeof p);char key[128],alt[20];p.lat=real_at(prefix,i,sky?"6":"lat",1000);p.lon=real_at(prefix,i,sky?"5":"lon",1000);if(absval(p.lat)>90||absval(p.lon)>180)continue;
 #define GET(field,k) path(key,prefix,i,k);text(key,p.field,sizeof p.field)
 GET(id,sky?"0":"hex");if(!p.id[0])continue;GET(callsign,sky?"1":"flight");{unsigned w=0;for(unsigned r=0;p.callsign[r];++r){char c=p.callsign[r];if(c==' '||c=='\t')continue;if(c>='a'&&c<='z')c-=32;p.callsign[w++]=c;}p.callsign[w]=0;}if(!p.callsign[0])cp(p.callsign,p.id,sizeof p.callsign);GET(registration,"r");GET(type,"t");GET(description,"desc");GET(country,sky?"2":"country");GET(squawk,sky?"14":"squawk");GET(category,sky?"17":"category");
 #undef GET
 path(key,prefix,i,sky?"8":"alt_baro");text(key,alt,sizeof alt);p.ground=eq(alt,sky?"true":"ground");if(p.ground)continue; /* The legacy nearby radar is always airborne-only. */p.alt=real_at(prefix,i,sky?"7":"alt_baro",-1);if(!sky&&p.alt>=0)p.alt/=3.28084;p.speed=real_at(prefix,i,sky?"9":"gs",0)/(sky?1:1.943844);p.heading=real_at(prefix,i,sky?"10":"track",-1);p.heading_valid=p.heading>=0;if(!p.heading_valid)p.heading=0;p.vertical=real_at(prefix,i,sky?"11":"baro_rate",0)*(sky?1:0.00508);path(key,prefix,i,"dbFlags");p.military=integer(key,0)&1;p.distance=distance(latitude,longitude,p.lat,p.lon);if(p.distance>range_km)continue;
 unsigned at=fresh_count;if(at==20){at=0;for(unsigned j=1;j<20;++j)if(fresh[j].distance>fresh[at].distance)at=j;if(p.distance>=fresh[at].distance)continue;}else ++fresh_count;fresh[at]=p;}
 svc->json_close();for(unsigned i=1;i<fresh_count;++i){Plane p=fresh[i];unsigned j=i;while(j&&fresh[j-1].distance>p.distance){fresh[j]=fresh[j-1];--j;}fresh[j]=p;}
 count=fresh_count;for(unsigned i=0;i<count;++i)planes[i]=fresh[i];selected=count;
 for(unsigned i=0;i<count;++i)if(eq(selected_id,planes[i].id))selected=i;
 if(selected==count){selected_id[0]=0;if(count){selected=0;cp(selected_id,planes[0].id,sizeof selected_id);}}
 record_trails();selected_route();route_budget=3;return 1;
}
static int airport_code(const char*s){unsigned n=len(s);if(n<3||n>4||eq(s,"NULL"))return 0;for(unsigned i=0;i<n;++i)if(!((s[i]>='A'&&s[i]<='Z')||(s[i]>='0'&&s[i]<='9')))return 0;return 1;}
static int merge_airport(const Airport*a){if(!airport_code(a->code)||a->lat!=a->lat||a->lon!=a->lon||absval(a->lat)>90||absval(a->lon)>180||distance(latitude,longitude,a->lat,a->lon)>range_km)return 0;unsigned j=0;while(j<airports.count&&!eq(airports.airports[j].code,a->code))++j;if(j==64)return 0;if(j<airports.count&&airports.airports[j].lat==a->lat&&airports.airports[j].lon==a->lon)return 0;airports.airports[j]=*a;if(j==airports.count)++airports.count;return 1;}
static void route_parse(const char*json,unsigned size){
 Route*r=&routes[route_slot];zero(r,sizeof *r);cp(r->call,route_call,sizeof r->call);r->expires=svc->monotonic_ms()+1800000;
 if(!json||svc->json_open(json,size)<0)return;const char*parts[]={"origin","destination"};char codes[2][9];zero(codes,sizeof codes);int changed=0;
 for(unsigned i=0;i<2;++i){char base[128],p[128];cp(base,"response/flightroute/",sizeof base);cat(base,parts[i],sizeof base);cp(p,base,sizeof p);cat(p,"/iata_code",sizeof p);text(p,codes[i],sizeof codes[i]);if(!airport_code(codes[i])){cp(p,base,sizeof p);cat(p,"/icao_code",sizeof p);text(p,codes[i],sizeof codes[i]);}if(!airport_code(codes[i]))codes[i][0]=0;
  cp(p,base,sizeof p);cat(p,"/name",sizeof p);text(p,i?r->destination:r->origin,97);if(!(i?r->destination:r->origin)[0]){cp(p,base,sizeof p);cat(p,"/municipality",sizeof p);text(p,i?r->destination:r->origin,97);}
  fold_latin(i?r->destination:r->origin);Airport a;zero(&a,sizeof a);cp(a.code,codes[i],sizeof a.code);cp(a.name,i?r->destination:r->origin,sizeof a.name);cp(p,base,sizeof p);cat(p,"/latitude",sizeof p);int valid=svc->json_real(p,&a.lat)==0;cp(p,base,sizeof p);cat(p,"/longitude",sizeof p);valid&=svc->json_real(p,&a.lon)==0;
  if(valid&&airport_code(a.code)&&absval(a.lat)<=90&&absval(a.lon)<=180){r->airports[r->airport_count++]=a;changed|=merge_airport(&a);}
 }
 if(codes[0][0]&&codes[1][0]){cp(r->route,codes[0],sizeof r->route);cat(r->route," -> ",sizeof r->route);cat(r->route,codes[1],sizeof r->route);r->expires=svc->monotonic_ms()+21600000;}
 svc->json_close();if(changed)svc->cache_write(&airports,sizeof airports);selected_route();
}
/* Reuse routes before spending the three-lookups-per-position-cycle budget.
 * Selected aircraft comes first; other aircraft also teach the airport cache. */
static int next_route(uint32_t now){
 int changed=0,pick=-1;unsigned oldest=0;
 for(unsigned n=0;n<count;++n){unsigned i=n==0&&selected<count?selected:n<=selected?n-1:n;if(i>=count)continue;unsigned j=0;for(;j<64;++j)if(eq(routes[j].call,planes[i].callsign))break;
  if(j<64&&!due(now,routes[j].expires)){for(unsigned a=0;a<routes[j].airport_count;++a)changed|=merge_airport(&routes[j].airports[a]);continue;}
  if(pick>=0||!route_budget)continue;pick=i;if(j==64){for(j=0;j<64;++j)if(!routes[j].call[0])break;if(j==64){for(unsigned k=1;k<64;++k)if((int32_t)(routes[k].expires-routes[oldest].expires)<0)oldest=k;j=oldest;}}route_slot=j;
 }
 if(changed)svc->cache_write(&airports,sizeof airports);return pick;
}
static void received(int32_t status,const char*json,uint32_t size,void*u){(void)u;busy=0;if(closing||generation!=request_generation)return;uint32_t now=svc->monotonic_ms();next_request=now+5000;
 if(request_kind==2){
  if(view!=5)return;
  int valid=0;double a=0,b=0;char city[64],region[64];city[0]=region[0]=0;
  if(status==200&&svc->json_open(json,size)==0){valid=boolean("success")&&svc->json_real("latitude",&a)==0&&svc->json_real("longitude",&b)==0&&absval(a)<=90&&absval(b)<=180;text("city",city,sizeof city);text("region",region,sizeof region);svc->json_close();}
  if(valid){edit_lat=a;edit_lon=b;edit_override=1;cp(location_notice,tr("Approximate: ","Ungefaehr: "),sizeof location_notice);cat(location_notice,city[0]?city:tr("location found","Standort gefunden"),sizeof location_notice);if(region[0]&&!eq(region,city)){cat(location_notice,", ",sizeof location_notice);cat(location_notice,region,sizeof location_notice);}}
  else cp(location_notice,tr("Location lookup failed. Try again or enter coordinates.","Ortssuche fehlgeschlagen. Erneut versuchen oder Koordinaten eingeben."),sizeof location_notice);
  dirty=1;return;
 }
 if(request_kind){route_parse(status==200?json:0,status==200?size:0);dirty=1;return;}
 if(status==200&&decode(json,size)){provider=request_provider;last_refresh=now;refresh_at=now+15000;notice[0]=0;}
 else if(request_provider<3){provider=request_provider+1;refresh_at=next_request;}
 else{provider=0;refresh_at=now+60000;cp(notice,tr("All aircraft providers failed; cached data","Alle Flugzeuganbieter fehlgeschlagen; Cache"),sizeof notice);}unsigned retry=svc->http_retry_seconds();if(retry>900)retry=900;if(retry*1000>next_request-now)next_request=now+retry*1000;dirty=1;
}
static void tick(void*u){(void)u;if(closing)return;uint32_t now=svc->monotonic_ms();if(view==0||view==2)resolve_location();if((view==0||view==2)&&now-last_paint>=1000){last_paint=now;dirty=1;}if(guess_pending&&view==5&&!busy&&due(now,next_request)){
 request_kind=2;request_generation=generation;int r=svc->http_get("https://ipwho.is/?fields=success,latitude,longitude,city,region",4096,received,0);
 if(!r){busy=1;guess_pending=0;}else if(r==GADGET_HTTP_BUSY||r==GADGET_HTTP_RATE_LIMIT)next_request=now+1000;else{guess_pending=0;received(r,0,0,0);}
 }
 if(configured&&(view==0||view==2)&&!busy&&due(now,next_request)){
 char url[512],a[40],b[40],n[40];int type=-1;
 if(due(now,refresh_at)){type=0;number(a,latitude,4);number(b,longitude,4);unsigned radius=(unsigned)(range_km/1.852+0.999);if(radius<1)radius=1;if(radius>250)radius=250;uint_text(n,radius);
 if(provider==0||provider==1){cp(url,provider==0?"https://api.adsb.lol/v2/point/":"https://api.airplanes.live/v2/point/",sizeof url);cat(url,a,sizeof url);cat(url,"/",sizeof url);cat(url,b,sizeof url);cat(url,"/",sizeof url);cat(url,n,sizeof url);}else if(provider==2){cp(url,"https://opendata.adsb.fi/api/v2/lat/",sizeof url);cat(url,a,sizeof url);cat(url,"/lon/",sizeof url);cat(url,b,sizeof url);cat(url,"/dist/",sizeof url);cat(url,n,sizeof url);}else{double dy=range_km/111,dx=range_km/(111*clamp(absval(cosval(latitude*PI/180)),0.2,1));cp(url,"https://opensky-network.org/api/states/all?lamin=",sizeof url);number(a,clamp(latitude-dy,-90,90),6);cat(url,a,sizeof url);cat(url,"&lomin=",sizeof url);number(a,clamp(longitude-dx,-180,180),6);cat(url,a,sizeof url);cat(url,"&lamax=",sizeof url);number(a,clamp(latitude+dy,-90,90),6);cat(url,a,sizeof url);cat(url,"&lomax=",sizeof url);number(a,clamp(longitude+dx,-180,180),6);cat(url,a,sizeof url);}}
 else{int pick=next_route(now);if(pick>=0){char encoded[80];if(encode(encoded,sizeof encoded,planes[pick].callsign)>=0){type=1;cp(route_call,planes[pick].callsign,sizeof route_call);cp(url,"https://api.adsbdb.com/v0/callsign/",sizeof url);cat(url,encoded,sizeof url);}}}
 if(type>=0){request_kind=type;request_provider=provider;request_generation=generation;int r=svc->http_get(url,196608,received,0);if(!r){busy=1;if(type==1&&route_budget)--route_budget;}else if(r!=GADGET_HTTP_BUSY&&r!=GADGET_HTTP_RATE_LIMIT)received(r,0,0,0);}}
 if(dirty&&ui->interaction_active(body?body:root)<=0){dirty=0;render();}}
static void shutdown(void){closing=1;++generation;root=body=input=keyboard=0;}
__attribute__((visibility("default"))) int gadget_app_entry(int argc,char**argv){gadget_app_lifecycle*life;if(init(argc,argv,&life))return -1;life->shutdown=shutdown;latitude=longitude=0;range_km=25;metric=1;hide_ground=override_location=0;count=trail_count=generation=provider=0;view=busy=guess_pending=range_open=0;location_notice[0]=0;selected_id[0]=notice[0]=route[0]=route_call[0]=origin[0]=destination[0]=0;char data[1024],s[40];int n=svc->config_read(data,sizeof data);if(n>0&&svc->json_open(data,n)==0){double v;if(text("center_latitude",s,sizeof s)>0&&decimal(s,&v)&&absval(v)<=90){latitude=v;override_location=1;}if(text("center_longitude",s,sizeof s)>0&&decimal(s,&v)&&absval(v)<=180)longitude=v;else override_location=0;if(text("range_km",s,sizeof s)>0&&decimal(s,&v)&&v>=0.1&&v<=20000)range_km=v;if(text("override_location",s,sizeof s)>=0)override_location=eq(s,"true");if(text("metric",s,sizeof s)>=0)metric=eq(s,"true");hide_ground=boolean("hide_ground");svc->json_close();}if(!override_location){gadget_environment env={sizeof env};svc->environment(&env);if(env.has_location){latitude=env.latitude_e6/1000000.0;longitude=env.longitude_e6/1000000.0;}}zero(&airports,sizeof airports);int bytes=svc->cache_read(&airports,sizeof airports);if(bytes<32||bytes<(int)(32+airports.count*sizeof(Airport))||!valid_airports())zero(&airports,sizeof airports);reset_area();resolve_location();next_request=svc->monotonic_ms();last_refresh=next_request-15000;zero(trails,sizeof trails);zero(routes,sizeof routes);route_budget=0;detail_id[0]=0;last_paint=0;rendered_view=-1;root=ui->create(0,GADGET_UI_ROOT,"",0,0);if(!root||ui->reserve(256)<0||!host->timer_create(250,tick,0))return -1;if(settings_entry)settings(0);render();dirty=0;return 0;}
#include "../sdk/gadget_math.h"
