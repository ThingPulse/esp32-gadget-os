/* Private helpers compiled into each package; no firmware app implementation. */
#ifndef GADGET_APP_H
#include "gadget_app.h"
#endif
#include <stddef.h>
static const gadget_host_api *host;
static const gadget_ui_api *ui;
static const gadget_services_api *svc;
static gadget_handle root,body,input,keyboard;
static int closing,dirty,settings_entry,german;
static unsigned len(const char*s){unsigned n=0;while(s&&s[n])++n;return n;}
static void cp(char*d,const char*s,unsigned cap){unsigned n=0;if(!cap)return;while(s&&s[n]&&n+1<cap){d[n]=s[n];++n;}d[n]=0;}
static int eq(const char*a,const char*b){while(*a&&*a==*b){++a;++b;}return *a==*b;}
static void cat(char*d,const char*s,unsigned cap){unsigned n=len(d);if(n<cap)cp(d+n,s,cap-n);}
static void zero(void*p,unsigned n){unsigned char*d=(unsigned char*)p;while(n--)*d++=0;}
static void uint_text(char*out,unsigned n){char b[12];unsigned i=0,k=0;do{b[i++]='0'+n%10;n/=10;}while(n);while(i)out[k++]=b[--i];out[k]=0;}
static const char*tr(const char*en,const char*de){return german?de:en;}
static int due(uint32_t t,uint32_t target){return (int32_t)(t-target)>=0;}
static gadget_handle make(gadget_handle p,unsigned kind,const char*t,int x,int y,int w,int h,gadget_callback cb,void*u){gadget_handle o=ui->create(p,kind,t,cb,u);ui->box(o,x,y,w,h);return o;}
static gadget_handle label(gadget_handle p,const char*t,int x,int y,int w,int h){gadget_handle o=make(p,GADGET_UI_LABEL,t,x,y,w,h,0,0);ui->style(o,GADGET_STYLE_ELLIPSIS);return o;}
static gadget_handle button(gadget_handle p,const char*t,int x,int y,int w,int h,gadget_callback cb,void*u){return make(p,GADGET_UI_BUTTON,t,x,y,w,h,cb,u);}
static void header(const char*title,gadget_callback back){ui->clear(root);body=input=keyboard=0;gadget_handle bar=make(root,GADGET_UI_CONTAINER,"",0,0,320,44,0,0);ui->style(bar,GADGET_STYLE_HEADER);ui->style(button(bar,"",0,0,44,44,back,0),GADGET_STYLE_BACK);label(bar,title,50,10,212,28);}
static void path(char*out,const char*prefix,unsigned i,const char*key){char n[12];uint_text(n,i);cp(out,prefix,128);cat(out,n,128);if(key&&*key){cat(out,"/",128);cat(out,key,128);}}
static int text(const char*p,char*out,unsigned cap){int n=svc->json_get(p,out,cap);if(n<0)out[0]=0;return n;}
static int integer(const char*p,int fallback){int32_t n;return svc->json_number(p,1,&n)==0?n:fallback;}
static int boolean(const char*p){char b[8];return text(p,b,sizeof b)>=0&&eq(b,"true");}
static int encode(char*out,unsigned cap,const char*s){unsigned n=0;const char*h="0123456789ABCDEF";for(;*s;++s){unsigned char c=*s;int plain=(c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='-'||c=='_'||c=='.'||c=='~';unsigned k=plain?1:3;if(n+k>=cap)return -1;if(plain)out[n++]=c;else{out[n++]='%';out[n++]=h[c>>4];out[n++]=h[c&15];}}out[n]=0;return n;}
/* Bounded JSON writer, propagates overflow instead of truncating saved data. */
typedef struct {char *data;unsigned size,cap;int failed;} Writer;
static void put(Writer*w,const char*s){unsigned n=len(s);if(w->failed||n>=w->cap-w->size){w->failed=1;return;}for(unsigned i=0;i<n;++i)w->data[w->size++]=s[i];w->data[w->size]=0;}
static void quote(Writer*w,const char*s){put(w,"\"");for(;*s;++s){unsigned char c=*s;char b[7]={0};if(c=='"'||c=='\\'){b[0]='\\';b[1]=c;}else if(c<32){const char*h="0123456789abcdef";cp(b,"\\u00",7);b[4]=h[c>>4];b[5]=h[c&15];}else b[0]=c;put(w,b);}put(w,"\"");}
static void field(Writer*w,const char*k,const char*v){quote(w,k);put(w,":");quote(w,v);}
static int init(int argc,char**argv,gadget_app_lifecycle**life){if(argc!=2||!argv)return -1;host=(const gadget_host_api*)argv[0];*life=(gadget_app_lifecycle*)argv[1];if(!host||host->major!=1||host->minor<10||host->struct_size<sizeof(*host)||!host->ui||host->ui->struct_size<sizeof(gadget_ui_api)||!host->services||host->services->struct_size<sizeof(gadget_services_api)||!*life||(*life)->struct_size<sizeof(**life))return -1;ui=host->ui;svc=host->services;closing=0;dirty=1;settings_entry=host->launch_mode()==GADGET_LAUNCH_SETTINGS;gadget_environment env={sizeof(env)};svc->environment(&env);german=env.german;(*life)->major=1;(*life)->minor=10;return 0;}
#ifdef __XTENSA__
void *memcpy(void*d,const void*s,size_t n){unsigned char*a=d;const unsigned char*b=s;while(n--)*a++=*b++;return d;}
#endif
