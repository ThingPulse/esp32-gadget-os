/* Header connectivity and request progress, shared by the migrated apps. */
static gadget_handle header_wifi,header_progress;
static void header_status_update(int busy){
 gadget_environment env;unsigned char*b=(unsigned char*)&env;for(unsigned i=0;i<sizeof env;++i)b[i]=0;env.struct_size=sizeof env;svc->environment(&env);
 ui->opacity(header_wifi,env.online?100:30);ui->visible(header_progress,busy!=0);
}
static void header_status(gadget_handle parent,int x,int busy){
 header_wifi=ui->create(parent,GADGET_UI_LABEL,"\xef\x87\xab",0,0);ui->box(header_wifi,x,13,22,22);
 header_progress=ui->create(parent,GADGET_UI_SPINNER,"",0,0);ui->box(header_progress,x-24,13,18,18);header_status_update(busy);
}
