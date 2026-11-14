/* Visual primitives shared by the migrated Planes and Transit screens. */
static gadget_handle caption(gadget_handle parent,const char*text,int x,int y,int w,int h,unsigned color,int large){
 gadget_handle o=label(parent,text,x,y,w,h);if(large)ui->style(o,GADGET_STYLE_TITLE);ui->paint(o,color,0,0);return o;
}
static gadget_handle card(gadget_handle parent,int x,int y,int w,int h,unsigned color,gadget_callback cb,void*u){
 gadget_handle o=make(parent,cb?GADGET_UI_BUTTON:GADGET_UI_CONTAINER,"",x,y,w,h,cb,u);ui->style(o,GADGET_STYLE_CARD);ui->paint(o,0xffffff,color,100);return o;
}
