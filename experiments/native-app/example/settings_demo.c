/* Developer fixture: one renderer shared by the in-app gear and Settings > Apps. */
#include "gadget_app.h"
static const gadget_host_api *host;
static gadget_handle root, value;
static int enabled;
static void render(int settings);
static void toggle(void *unused) {
    (void)unused;
    int next=!enabled;
    if(host->setting_set("enabled",next?"1":"0")<0)return;
    enabled=next;host->ui->text(value,enabled?"Enabled":"Disabled");
}
static void back(void *unused) {
    (void)unused;
    if(host->launch_mode()==GADGET_LAUNCH_SETTINGS)host->request_close();
    else render(0);
}
static void gear(void *unused){(void)unused;render(1);}
static void close_app(void *unused){(void)unused;host->request_close();}
static void render(int settings) {
    host->ui->clear(root);
    host->ui->create(root,GADGET_UI_LABEL,settings?"Demo settings":"Settings demo",0,0);
    if(settings){
        value=host->ui->create(root,GADGET_UI_LABEL,enabled?"Enabled":"Disabled",0,0);
        host->ui->create(root,GADGET_UI_BUTTON,"Toggle",toggle,0);
        host->ui->create(root,GADGET_UI_BUTTON,"Back",back,0);
    }else{
        host->ui->create(root,GADGET_UI_BUTTON,"Settings",gear,0);
        host->ui->create(root,GADGET_UI_BUTTON,"Close",close_app,0);
    }
}
static void shutdown_app(void){root=value=0;}
__attribute__((visibility("default")))
int gadget_app_entry(int argc,char **argv){
    if(argc!=2||!argv)return -1;
    host=(const gadget_host_api*)argv[0];
    gadget_app_lifecycle *app=(gadget_app_lifecycle*)argv[1];
    if(!host||host->struct_size<sizeof(*host)||host->major!=1||host->minor<8||!host->launch_mode||!app||app->struct_size<sizeof(*app))return -2;
    app->major=1;app->minor=8;app->shutdown=shutdown_app;
    char stored[2];enabled=host->setting_get("enabled",stored,sizeof(stored))==1&&stored[0]=='1';
    root=host->ui->create(0,GADGET_UI_ROOT,"",0,0);
    if(!root)return -3;
    host->ui->flow(root,2,12,12);
    render(host->launch_mode()==GADGET_LAUNCH_SETTINGS);
    return 0;
}
