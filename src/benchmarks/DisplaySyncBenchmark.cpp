#if defined(GADGET_OS_DISPLAY_SYNC_BENCHMARK) && GADGET_OS_DISPLAY_SYNC_BENCHMARK
#include <Arduino.h>
#include <esp_heap_caps.h>
#include <driver/gpio.h>
#include <esp_rom_gpio.h>
#include <soc/gpio_sig_map.h>
#include <algorithm>
#include <ColorKitPro.h>
#include "hal/ProDisplayTransfer.h"

namespace {
constexpr uint32_t WriteClockHz=20000000;
ColorKitPro lcd(WriteClockHz);
lv_display_t *display;
uint8_t *strip, *frame;
bool staged, synchronized, scan_valid;
uint32_t scan_period, transfer_us, wait_us, timeout_count, flushes;
constexpr unsigned Samples=90, Warmup=10;
constexpr int Width=320, Height=480, Rows=64;
uint32_t tick(){return millis();}
bool slow_readback=false;
constexpr int DataPins[]={PIN_DB0,PIN_DB1,PIN_DB2,PIN_DB3,PIN_DB4,PIN_DB5,PIN_DB6,PIN_DB7,
  PIN_DB8,PIN_DB9,PIN_DB10,PIN_DB11,PIN_DB12,PIN_DB13,PIN_DB14,PIN_DB15};
uint16_t read_gpio_word()
{
  uint16_t word=0;for(unsigned i=0;i<16;++i)word|=gpio_get_level((gpio_num_t)DataPins[i])<<i;
  return word;
}
void slow_register(uint8_t command,uint16_t *words,unsigned count)
{
  // Diagnostic only: explicitly release peripheral-controlled output enables,
  // allow 2 us settling, and inspect both data lanes before interpreting bytes.
  lcd.startWrite();lcd.writeCommand(command);
  auto *bus=lcd.panel()->bus();bus->beginRead();
  for(int pin:DataPins){
    gpio_set_direction((gpio_num_t)pin,GPIO_MODE_INPUT);
    esp_rom_gpio_connect_out_signal(pin,SIG_GPIO_OUT_IDX,false,false);
  }
  gpio_set_level((gpio_num_t)PIN_TFT_RS,1);
  delayMicroseconds(2);gpio_set_level((gpio_num_t)PIN_TFT_RD,1);delayMicroseconds(2); // dummy
  for(unsigned i=0;i<count;++i){
    gpio_set_level((gpio_num_t)PIN_TFT_RD,0);delayMicroseconds(2);
    words[i]=read_gpio_word();gpio_set_level((gpio_num_t)PIN_TFT_RD,1);delayMicroseconds(2);
  }
  gpio_set_level((gpio_num_t)PIN_TFT_CS,1);
  for(int pin:DataPins)gpio_set_direction((gpio_num_t)pin,GPIO_MODE_INPUT_OUTPUT);
  bus->endRead();lcd.endWrite();
}
int scan(){
  if(!slow_readback)return lcd.panel()->getScanLine();
  uint16_t words[2];slow_register(0x45,words,2);return ((words[0]&255)<<8)|(words[1]&255);
}
void probe()
{
  Serial.printf("SCAN_REG,id=%08lx,power=%08lx,madctl=%08lx\n",
    (unsigned long)lcd.panel()->readCommand(0xD3,0,4),
    (unsigned long)lcd.panel()->readCommand(0x0A,0,1),
    (unsigned long)lcd.panel()->readCommand(0x0B,0,1));
  bool seen[2048]={}; unsigned valid=0,invalid=0,unique=0,wraps=0;
  uint32_t start=micros(),read_total=0,read_max=0,last_wrap=0,periods[64]={};
  int previous=-1,minimum=65535,maximum=-1;
  while(uint32_t(micros()-start)<600000){
    uint32_t t=micros();int n=scan();uint32_t elapsed=micros()-t;
    read_total+=elapsed;read_max=std::max(read_max,elapsed);
    if(n>=0 && n<2048){
      ++valid;minimum=std::min(minimum,n);maximum=std::max(maximum,n);
      if(!seen[n]){seen[n]=true;++unique;}
      if(previous>200 && n<100){
        uint32_t now=micros();if(last_wrap && wraps<64)periods[wraps++]=now-last_wrap;last_wrap=now;
      }
      previous=n;
    }else{++invalid;previous=-1;}
    if(valid+invalid<=24)Serial.printf("SCAN_SAMPLE,%d\n",n);
    delayMicroseconds(100);
    if((valid+invalid)%32==0)delay(1);
  }
  if(wraps){std::sort(periods,periods+wraps);scan_period=periods[wraps/2];}
  scan_valid=invalid==0 && unique>100 && maximum>=479 && maximum<1024 && wraps>=5 && scan_period>=5000 && scan_period<=50000;
  Serial.printf("SCAN_PROBE,reader=%s,valid=%u,invalid=%u,unique=%u,min=%d,max=%d,wraps=%u,period_us=%u,read_mean_us=%.2f,read_max_us=%u,usable=%u\n",
    slow_readback?"gpio":"library",valid,invalid,unique,minimum,maximum,wraps,scan_period,double(read_total)/(valid+invalid),read_max,scan_valid);
}
bool wait_for_wrap()
{
  if(!scan_valid)return false;
  uint32_t start=micros();int previous=scan();
  uint32_t deadline=std::min<uint32_t>(50000U,scan_period*2+3000);
  while(uint32_t(micros()-start)<deadline){
    int current=scan();
    if(current<0 || current>=1024)return false;
    if(previous>200 && current<100)return true;
    previous=current;delayMicroseconds(30);
  }
  return false;
}
void flush(lv_display_t *d,const lv_area_t *area,uint8_t *pixels)
{
  ++flushes;
  if(!staged){
    uint32_t t=micros();pro_display_write(lcd,*area,pixels,true);transfer_us+=micros()-t;
  }else{
    // FULL render mode gives one complete native RGB565 frame in PSRAM.
    if(area->x1!=0 || area->y1!=0 || area->x2!=Width-1 || area->y2!=Height-1){
      Serial.println("SYNC_ERROR,unexpected_flush_area");lv_display_flush_ready(d);return;
    }
    if(synchronized){
      uint32_t t=micros();if(!wait_for_wrap())++timeout_count;wait_us+=micros()-t;
    }
    uint32_t t=micros();
    for(int y=0;y<Height;y+=Rows){
      int count=std::min(Rows,Height-y);
      memcpy(strip,pixels+y*Width*2,count*Width*2);
      lv_area_t band={0,y,Width-1,y+count-1};
      pro_display_write(lcd,band,strip,true);
    }
    transfer_us+=micros()-t;
  }
  lv_display_flush_ready(d);
}
lv_obj_t *box(lv_obj_t *parent,int x,int y,int w,int h,uint32_t color)
{
  auto *o=lv_obj_create(parent);lv_obj_remove_style_all(o);lv_obj_set_pos(o,x,y);lv_obj_set_size(o,w,h);
  lv_obj_set_style_bg_color(o,lv_color_hex(color),0);lv_obj_set_style_bg_opa(o,LV_OPA_COVER,0);
  lv_obj_set_style_radius(o,10,0);lv_obj_remove_flag(o,LV_OBJ_FLAG_SCROLLABLE);return o;
}
void text(lv_obj_t *parent,const char *value,int x,int y)
{
  auto *l=lv_label_create(parent);lv_label_set_text(l,value);lv_obj_set_pos(l,x,y);lv_obj_set_style_text_color(l,lv_color_white(),0);
}
lv_obj_t *scene()
{
  auto *screen=lv_screen_active();lv_obj_clean(screen);lv_obj_set_style_bg_color(screen,lv_color_hex(0x182131),0);lv_obj_remove_flag(screen,LV_OBJ_FLAG_SCROLLABLE);
  auto *pages=box(screen,0,0,Width*2,Height,0x182131);
  for(int page=0;page<2;++page){
    text(pages,page?"Lights and sensors":"Home display benchmark",Width*page+12,12);
    for(int row=0;row<7;++row){
      auto *card=box(pages,Width*page+12,46+row*52,Width-24,48,0x253449);
      char title[48];snprintf(title,sizeof title,"Room %d - ceiling light",page*7+row+1);
      text(card,title,12,3);text(card,"On    Brightness 65%",12,25);
    }
  }
  return pages;
}
void diagnose_slow_readback()
{
  uint16_t words[4];slow_register(0xD3,words,4);
  Serial.printf("SCAN_SLOW_ID,%04x,%04x,%04x,%04x\n",words[0],words[1],words[2],words[3]);
  slow_register(0x0A,words,1);Serial.printf("SCAN_SLOW_POWER,%04x\n",words[0]);
  slow_register(0x0B,words,1);Serial.printf("SCAN_SLOW_MADCTL,%04x\n",words[0]);
  slow_readback=true;probe();
}
void profile(unsigned mode)
{
  staged=mode!=0;synchronized=mode==2;
  lv_display_set_buffers(display,staged?frame:strip,nullptr,staged?Width*Height*2:Width*Rows*2,
    staged?LV_DISPLAY_RENDER_MODE_FULL:LV_DISPLAY_RENDER_MODE_PARTIAL);
}
void run(unsigned mode,unsigned repeat)
{
  const char *names[]={"strip-64","frame-unsynced","frame-scan-sync"};
  if(mode==2 && !scan_valid){Serial.println("SYNC_SKIP,frame-scan-sync,scan_readback_invalid");return;}
  profile(mode);auto *pages=scene();uint32_t times[Samples];uint64_t total=0,io=0,wait=0;unsigned timeouts=0,calls=0;
  for(unsigned i=0;i<Warmup+Samples;++i){
    lv_obj_set_x(pages,-int((i*7)%Width));lv_obj_invalidate(lv_screen_active());
    transfer_us=wait_us=timeout_count=flushes=0;uint32_t t=micros();lv_refr_now(display);uint32_t elapsed=micros()-t;
    if(i>=Warmup){times[i-Warmup]=elapsed;total+=elapsed;io+=transfer_us;wait+=wait_us;timeouts+=timeout_count;calls+=flushes;}
    delay(1);
  }
  std::sort(times,times+Samples);
  Serial.printf("SYNC_RESULT,%s,%u,%u,%.2f,%u,%u,%u,%.2f,%.2f,%u,%.2f\n",names[mode],repeat,Samples,
    double(total)/Samples,times[(Samples-1)/2],times[(Samples*95+99)/100-1],times[Samples-1],double(io)/Samples,double(wait)/Samples,timeouts,double(calls)/Samples);
}
unsigned demo_mode=0,demo_frame=0;lv_obj_t *demo_pages=nullptr;
void demo(unsigned mode)
{
  if(mode==2 && !scan_valid){Serial.println("SYNC_SKIP,frame-scan-sync,scan_readback_invalid");return;}
  profile(mode);demo_pages=scene();demo_mode=mode;demo_frame=0;
  text(lv_screen_active(),mode==0?"1: strips":mode==1?"2: full frame":"3: scan sync",12,445);
  Serial.printf("SYNC_DEMO,%u\n",mode+1);
}
}
void setup()
{
  Serial.begin(115200);delay(5000);lcd.init();lcd.setRotation(0);lcd.setColorDepth(16);lcd.setBrightness(180);
  Serial.printf("SYNC_META,portrait=320x480,clock_hz=%u,network=off\n",unsigned(WriteClockHz));probe();
  if(!scan_valid)diagnose_slow_readback();
  strip=(uint8_t*)heap_caps_malloc(Width*Rows*2,MALLOC_CAP_INTERNAL|MALLOC_CAP_DMA|MALLOC_CAP_8BIT);
  frame=(uint8_t*)heap_caps_malloc(Width*Height*2,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
  if(!strip || !frame){Serial.println("SYNC_ERROR,allocation_failed");return;}
  lv_init();lv_tick_set_cb(tick);display=lv_display_create(Width,Height);lv_display_set_color_format(display,LV_COLOR_FORMAT_RGB565);lv_display_set_flush_cb(display,flush);lv_sysmon_hide_performance(display);
  Serial.printf("SYNC_MEMORY,strip_bytes=%u,frame_bytes=%u,internal_free=%u,psram_free=%u\n",Width*Rows*2,Width*Height*2,
    (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT),(unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
  Serial.println("SYNC_HEADER,profile,repeat,frames,mean_us,p50_us,p95_us,max_us,transfer_mean_us,wait_mean_us,timeouts,flushes_mean");
  for(unsigned repeat=0;repeat<3;++repeat)for(unsigned n=0;n<3;++n)run((n+repeat)%3,repeat);
  Serial.println("SYNC_DONE");demo(scan_valid?2:1);
}
void loop()
{
  if(!display){delay(100);return;}
  if(Serial.available()){char c=Serial.read();if(c>='1' && c<='3')demo(c-'1');}
  // Ping-pong motion avoids a discontinuity at the page boundary during visual checks.
  unsigned phase=(demo_frame++*5)%(Width*2);int x=phase<Width?phase:Width*2-phase;
  lv_obj_set_x(demo_pages,-x);lv_obj_invalidate(lv_screen_active());lv_refr_now(display);delay(1);
}
#endif
