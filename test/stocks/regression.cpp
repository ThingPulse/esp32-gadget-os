#include <array>
#include <atomic>
#include <string>
#include <cassert>
#include <cmath>
#include <fstream>
#include <thread>
#include <chrono>
#include <cstdlib>
#include "services/StockDataProvider.h"
#define private public
#include "services/StockService.h"
#undef private
#include "../../src/apps/StockTickerApp.cpp"

static uint8_t frame[320*480*3];
static uint32_t tick;
static void capture(const char *name) {
    lv_obj_update_layout(lv_screen_active());lv_refr_now(nullptr);
    if(const char *dir=std::getenv("STOCK_PREVIEW_DIR")) {
        std::ofstream f(std::string(dir)+"/"+name+".ppm",std::ios::binary);
        f<<"P6\n320 480\n255\n";f.write(reinterpret_cast<char *>(frame),sizeof(frame));
    }
}
class Fixture : public stocks::StockDataProvider {
public:
    stocks::Error failure=stocks::Error::None;
    const char *id() const override {return "yahoo";}
    const char *name() const override {return "Fixture data";}
    stocks::Result fetch(const std::string &symbol,stocks::Period p) override {
        stocks::Result r;
        if(failure!=stocks::Error::None) {r.error=failure;r.message="Fixture failure";r.retry_after_ms=120000;return r;}
        auto &q=r.quote;q.symbol=symbol;q.name="Apple";q.currency="USD";q.exchange="NASDAQ";
        q.price=230;q.previous_close=227;q.has_previous_close=true;q.timestamp=1792767600;q.period=p;
        q.count=30;for(size_t i=0;i<q.count;++i) {q.samples[i].time=1790000000+i*86400;q.samples[i].price=220+i*.3+std::sin(i)*2;}
        return r;
    }
} fixture;
class Alternative : public Fixture {
public:
    const char *id() const override {return "alternative";}
    bool supports(stocks::Period p) const override {return p!=stocks::Period::Year;}
} alternative;
static void finish(StockService &s) {
    for(int i=0;i<500 && s._running;++i) {std::this_thread::sleep_for(std::chrono::milliseconds(2));s.loop();}
    assert(!s._running);
}
int main(int argc,char **argv) {
    using namespace stocks;
    std::array<std::string,MaxSymbols> symbols;size_t n=0;std::string error;
    assert(parse_watchlist("aapl\nNESN.SW\n7203.T",symbols,n,error) && n==3 && symbols[0]=="AAPL");
    assert(!parse_watchlist("AAPL aapl",symbols,n,error));
    assert(!parse_watchlist("AAPL;touch",symbols,n,error));
    assert(!parse_watchlist("A B C D E F G H I J K",symbols,n,error));
    assert(parse_watchlist("",symbols,n,error) && n==0);
    assert(price_text(75,"GBp")=="75.00 GBp");
    const std::string payload=R"({"chart":{"result":[{"meta":{"symbol":"AAPL","currency":"USD","regularMarketPrice":230,"regularMarketTime":259300,"gmtoffset":0,"chartPreviousClose":100},"timestamp":[86401,172801,259201],"indicators":{"quote":[{"close":[225,227,230]}]}}],"error":null}})";
    auto r=YahooStockProvider::parse(payload,"AAPL",Period::FiveDays);
    assert(r.ok() && r.quote.count==3 && r.quote.previous_close==227);
    assert(change_text(r.quote,true)=="+1.32%");
    // Curves pass through observations and never introduce new highs/lows.
    stocks::Quote curve;
    curve.count=5;
    const double values[]={100,102,98,98,104};
    for(size_t i=0;i<curve.count;++i) curve.samples[i].price=values[i];
    for(size_t i=0;i+1<curve.count;++i) for(int step=0;step<=20;++step) {
        const double y=stock_chart::sample(curve,i,step/20.0);
        assert(y>=std::min(values[i],values[i+1]) && y<=std::max(values[i],values[i+1]));
    }
    curve.samples[2].price=NAN;
    assert(std::isnan(stock_chart::sample(curve,1,0.5)));
    assert(stock_chart::sample(curve,1,0)==102);

    assert(!YahooStockProvider::parse(payload,"NESN.SW",Period::FiveDays).ok());
    assert(!YahooStockProvider::parse("<html>blocked</html>","AAPL",Period::FiveDays).ok());
    assert(!YahooStockProvider::parse(std::string(65537,'x'),"AAPL",Period::FiveDays).ok());
    assert(!YahooStockProvider::parse(R"({"chart":{"result":null,"error":{"code":"Not Found"}}})","AAPL",Period::FiveDays).ok());
    std::string missing=payload;auto start=missing.find("[225,227,230]");missing.replace(start,13,"[null,null,230]");
    r=YahooStockProvider::parse(missing,"AAPL",Period::FiveDays);
    assert(r.ok() && !r.quote.has_previous_close && std::isnan(r.quote.samples[0].price));
    std::string long_series=R"({"chart":{"result":[{"meta":{"symbol":"AAPL","currency":"USD","regularMarketPrice":230,"regularMarketTime":30000000,"previousClose":230},"timestamp":[)";
    for(int i=1;i<=250;++i) {if(i>1)long_series+=",";long_series+=std::to_string(i*86400);}
    long_series+=R"(],"indicators":{"quote":[{"close":[)";
    for(int i=0;i<250;++i) {if(i)long_series+=",";long_series+="230";}
    long_series+="]}]}}]}}";
    r=YahooStockProvider::parse(long_series,"AAPL",Period::Year);
    assert(r.ok() && r.quote.count==MaxSamples && r.quote.samples.back().time==250*86400);
    assert(change_text(r.quote,true)=="+0.00%");
    if(std::getenv("STOCK_TEST_LIVE")) {
        YahooStockProvider live;
        r=live.fetch("AAPL",Period::FiveDays);assert(r.ok() && r.quote.symbol=="AAPL");
        std::puts("Native Yahoo HTTPS transport passed");
    }
    if(argc>1) {
        std::ifstream f(argv[1]);std::string body((std::istreambuf_iterator<char>(f)),{});
        r=YahooStockProvider::parse(body,"AAPL",Period::FiveDays);assert(r.ok());
        assert(r.quote.has_previous_close);std::printf("Live fixture: %s %s\n",price_text(r.quote.price,r.quote.currency).c_str(),change_text(r.quote).c_str());
    }
    auto &s=StockService::instance();assert(s.begin());assert(s.set_provider(fixture));
    assert(s.save_watchlist("AAPL\nNESN.SW\n7203.T\nSAP.DE\nVOD.L",error));
    s.resume();s.loop();finish(s);assert(s.entry(0).valid);
    assert(s.refresh(0)==StockService::Refresh::Recent);
    s._entries[0].fetched-=31000;
    const auto gate=s._gate;
    assert(s.refresh(0)==StockService::Refresh::Queued);
    assert(s.refresh(0)==StockService::Refresh::Queued && s._gate==gate);
    s.loop();assert(!s.busy()); // Manual refresh cannot bypass request spacing.
    s._gate_due=true;s.loop();finish(s);
    assert(s.entry(0).valid && !s.entry(0).refresh_pending);
    // Persisted list editing/reordering retains matching cached entries.
    assert(s.save_watchlist("NESN.SW\nAAPL",error));assert(s.entry(1).valid);
    s._gate_due=true;s.loop();assert(s.busy());
    assert(s.save_watchlist("AAPL",error));finish(s);assert(s.count()==1 && s.entry(0).symbol=="AAPL");
    assert(s.set_provider(fixture));assert(!s.entry(0).valid);
    fixture.failure=Error::Throttled;
    s._gate_due=true;s.loop();finish(s);
    assert(!s.entry(0).valid && s.entry(0).error=="Fixture failure");
    assert(int32_t(s.entry(0).deadline-StockService::now_ms())>110000);
    assert(!s.entry(0).due);
    const auto retry_deadline=s.entry(0).deadline, retry_gate=s._gate;
    assert(s.refresh(0)==StockService::Refresh::RetryLater);
    s.select(0,Period::Month);
    assert(!s.entry(0).due && s.entry(0).deadline==retry_deadline && s._gate==retry_gate);
    s.loop();assert(!s.busy());
    fixture.failure=Error::None;
    assert(s.set_provider(alternative));s.loop();
    assert(!s.busy() && !s.entry(0).error.empty());
    assert(!s.supports(Period::Year));
    assert(s.save_watchlist("AAPL",error));s.loop();finish(s);assert(s.entry(0).valid);
    assert(s.set_provider(fixture));
    s.pause();s.loop();assert(!s.busy());
    assert(s.save_watchlist("AAPL\nNESN.SW\n7203.T\nSAP.DE\nVOD.L",error));
    const char *names[]={"Apple","Nestle","Toyota","SAP","Vodafone"};
    const char *currencies[]={"USD","CHF","JPY","EUR","GBp"};
    const double prices[]={230,80,2750,220,75}, previous[]={227,80.5,2730,216,75.4};
    for(size_t i=0;i<s.count();++i) {
        auto &e=s._entries[i];e.quote=fixture.fetch(e.symbol,Period::FiveDays).quote;
        e.quote.name=names[i];e.quote.currency=currencies[i];e.quote.price=prices[i];e.quote.previous_close=previous[i];
        e.valid=true;e.fetched=StockService::now_ms();e.due=false;e.deadline=e.fetched+300000;
    }
    lv_init();lv_tick_set_cb([](){return tick;});auto *display=lv_display_create(320,480);
    lv_obj_add_flag(lv_layer_sys(),LV_OBJ_FLAG_HIDDEN);static uint8_t buffer[320*40*4];
    lv_display_set_color_format(display,LV_COLOR_FORMAT_XRGB8888);
    lv_display_set_buffers(display,buffer,nullptr,sizeof(buffer),LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(display,[](lv_display_t *d,const lv_area_t *a,uint8_t *pixels){
        for(int y=a->y1;y<=a->y2;++y)for(int x=a->x1;x<=a->x2;++x) {
            int src=((y-a->y1)*(a->x2-a->x1+1)+x-a->x1)*4,dst=(y*320+x)*3;
            frame[dst]=pixels[src+2];frame[dst+1]=pixels[src+1];frame[dst+2]=pixels[src];
        }lv_display_flush_ready(d);
    });
    stock_ticker_app_launch();capture("stocks-watchlist");
    assert(lv_obj_get_child_count(ctx.body)==10);
    lv_obj_send_event(lv_obj_get_child(ctx.body,2),LV_EVENT_CLICKED,nullptr);
    assert(ctx.view==1);capture("stocks-detail");
    refresh(nullptr);assert(!lv_obj_has_flag(ctx.notice,LV_OBJ_FLAG_HIDDEN));
    back(nullptr);assert(ctx.view==0);
    // Real pointer input exercises elastic pulling even when the list fits.
    static lv_indev_data_t pointer{};
    auto *input=lv_indev_create();lv_indev_set_type(input,LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(input,[](lv_indev_t *,lv_indev_data_t *data){*data=pointer;});
    for(size_t i=0;i<s.count();++i) s._entries[i].fetched-=31000;
    lv_obj_update_layout(ctx.screen);
    pointer.point={160,115};pointer.state=LV_INDEV_STATE_PRESSED;lv_indev_read(input);
    for(int y=120;y<=360;y+=10) {pointer.point.y=y;tick+=20;lv_indev_read(input);lv_timer_handler();}
    pointer.state=LV_INDEV_STATE_RELEASED;lv_indev_read(input);
    for(int i=0;i<50;++i) {tick+=20;lv_timer_handler();}
    for(size_t i=0;i<s.count();++i) assert(s.entry(i).refresh_pending);
    assert(!ctx.scrolling && !ctx.pull_armed);
    lv_indev_delete(input);
    edit(nullptr);capture("stocks-editor");
    lv_textarea_set_text(ctx.input,"AAPL\nAAPL");save(nullptr);assert(ctx.view==2);
    lv_textarea_set_text(ctx.input,"AAPL\nNESN.SW");save(nullptr);assert(ctx.view==0 && s.count()==2);
    auto *screen=ctx.screen;back(nullptr);lv_obj_delete(screen);assert(!s._active && !ctx.timer);
    std::puts("Stock provider, persistence, worker and UI regressions passed");
}
