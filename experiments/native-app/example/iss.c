/* Independent app: no firmware includes, networking implementation or LVGL ABI.
 * Host owns HTTPS/JSON primitives. App owns endpoint, fields, UI and lifecycle. */
#include "gadget_app.h"
#include <stddef.h>
static const gadget_host_api *host;
static gadget_handle details,status_label;
static unsigned pending,cooldown,age,has_data;
typedef struct {char text[160]; unsigned n;} Text;
static void add(Text *t,const char *s) {while (*s && t->n<159) t->text[t->n++]=*s++; t->text[t->n]=0;}
static void number(Text *t,int32_t value,unsigned decimals)
{
    char digits[12]; unsigned n=0;
    if (value<0) {add(t,"-"); value=-value;}
    unsigned v=(unsigned)value;
    do {digits[n++]=(char)('0'+v%10); v/=10;} while(v || n<=decimals);
    while(n) {char s[2]={digits[--n],0}; add(t,s); if(n && n==decimals) add(t,".");}
}
static void show_age(void)
{
    Text t; t.n=0; t.text[0]=0; add(&t,"Updated "); number(&t,(int32_t)age,0); add(&t," s ago");
    host->label_set_text(status_label,t.text);
}
static void received(int32_t code,const char *json,uint32_t size,void *user)
{
    (void)user; pending=0;
    if (code!=200) {
        has_data=0;
        host->label_set_text(details,"Position unavailable");
        host->label_set_text(status_label,code==429?"Service busy. Retry in a minute.":"Request failed. Tap Refresh to retry.");
        if(code==429) cooldown=60;
        host->log("ISS request failed"); return;
    }
    int32_t lat,lon,alt,speed;
    if(host->json_number_scaled(json,size,"latitude",1000,&lat) ||
       host->json_number_scaled(json,size,"longitude",1000,&lon) ||
       host->json_number_scaled(json,size,"altitude",10,&alt) ||
       host->json_number_scaled(json,size,"velocity",1,&speed) ||
       lat < -90000 || lat > 90000 || lon < -180000 || lon > 180000 ||
       alt < 0 || alt > 100000 || speed < 0 || speed > 100000) {
        has_data=0; host->label_set_text(details,"Position unavailable");
        host->label_set_text(status_label,"Invalid response. Tap Refresh to retry.");
        host->log("ISS invalid response"); return;
    }
    Text t; t.n=0; t.text[0]=0;
    add(&t,"Latitude: "); number(&t,lat,3);
    add(&t,"\nLongitude: "); number(&t,lon,3);
    add(&t,"\nAltitude: "); number(&t,alt,1); add(&t," km");
    add(&t,"\nSpeed: "); number(&t,speed,0); add(&t," km/h");
    host->label_set_text(details,t.text); age=0; has_data=1; show_age();
    host->log("ISS position updated");
}
static void refresh(void *user)
{
    (void)user;
    if(pending) return;
    if(cooldown) {host->label_set_text(status_label,"Please wait before refreshing."); return;}
    host->label_set_text(status_label,"Loading position...");
    int32_t result=host->http_get_json("https://api.wheretheiss.at/v1/satellites/25544",received,0);
    cooldown=5;
    if(result) {
        host->label_set_text(status_label,result==GADGET_HTTP_OFFLINE?"No Wi-Fi. Tap Refresh when connected.":
            result==GADGET_HTTP_MEMORY?"Not enough memory for HTTPS.":"Network busy. Retry in a few seconds.");
        host->log("ISS request not started");
    } else {pending=1; host->log("ISS request started");}
}
static void tick(void *user)
{
    (void)user;
    if(cooldown) --cooldown;
    if(has_data && age<86400) ++age;
    if(has_data && !pending && !cooldown) show_age();
}
static void close_app(void *user)
{
    (void)user; host->log("ISS close received"); host->request_close();
}
static void shutdown(void)
{
    host->log("ISS shutdown"); pending=0; host=0;
}
__attribute__((visibility("default"))) int gadget_app_entry(int argc,char **argv)
{
    if(argc!=2 || !argv) return -1;
    host=(const gadget_host_api*)argv[0];
    gadget_app_lifecycle *app=(gadget_app_lifecycle*)argv[1];
    if(!host || host->struct_size<offsetof(gadget_host_api,ui) || host->major!=1 || host->minor<1 ||
       !host->http_get_json || !host->json_number_scaled || !app || app->struct_size<sizeof(*app)) return -1;
    app->major=1; app->minor=1; app->shutdown=shutdown;
    pending=cooldown=age=has_data=0;
    gadget_handle screen=host->screen_create();
    if(!screen || !host->label_create(screen,"ISS Position")) return -1;
    details=host->label_create(screen,"Fetching the space station's position");
    status_label=host->label_create(screen,"Starting...");
    if(!details || !status_label || !host->button_create(screen,"Refresh",refresh,0) ||
       !host->button_create(screen,"Close",close_app,0) || !host->timer_create(1000,tick,0)) return -1;
    host->log("ISS initialized from filesystem ELF"); refresh(0); return 0;
}
