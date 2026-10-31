#if defined(GADGET_OS_DISPLAY_BENCHMARK) && GADGET_OS_DISPLAY_BENCHMARK
#include <Arduino.h>
#include <esp_heap_caps.h>
#include <algorithm>
#include <ColorKitPro.h>
#include "hal/ProDisplayTransfer.h"

namespace {
ColorKitPro lcd;
lv_display_t *display;
uint8_t *buffer;
bool bulk_dma, buffer_internal;
uint64_t io_us, pixels;
uint32_t flushes;
constexpr unsigned Samples = 90, Warmup = 10, Repeats = 3;
struct Profile { const char *name; unsigned rows; bool internal, dma; };
const Profile profiles[] = {
  {"baseline-8-psram", 8, false, false},
  {"internal-8", 8, true, false},
  {"internal-32", 32, true, false},
  {"dma-32", 32, true, true},
  {"dma-64", 64, true, true},
};
uint32_t tick() { return millis(); }
void flush(lv_display_t *d, const lv_area_t *area, uint8_t *data)
{
  uint32_t start = micros();
  pro_display_write(lcd, *area, data, bulk_dma);
  io_us += uint32_t(micros() - start);
  pixels += lv_area_get_size(area);
  ++flushes;
  lv_display_flush_ready(d);
}
void reset_counters() { io_us = pixels = flushes = 0; }
void report(const Profile &p, const char *scene, unsigned repeat, uint32_t *times,
            uint64_t total, uint64_t io, uint64_t px, uint64_t calls)
{
  std::sort(times, times + Samples);
  // Entire synchronous refresh, including first-strip render and final DMA wait.
  Serial.printf("BENCH,%s,%s,%u,%u,%.2f,%u,%u,%u,%.2f,%.2f,%.2f\n",
      p.name, scene, repeat, Samples, double(total) / Samples,
      times[(Samples - 1) / 2], times[(Samples * 95 + 99) / 100 - 1], times[Samples - 1],
      double(io) / Samples, double(calls) / Samples, double(px) / Samples);
}
lv_obj_t *box(lv_obj_t *parent, int x, int y, int w, int h, uint32_t color)
{
  auto *obj = lv_obj_create(parent);
  lv_obj_remove_style_all(obj);
  lv_obj_set_pos(obj, x, y); lv_obj_set_size(obj, w, h);
  lv_obj_set_style_bg_color(obj, lv_color_hex(color), 0);
  lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(obj, 10, 0);
  lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
  return obj;
}
void text(lv_obj_t *parent, const char *value, int x, int y)
{
  auto *label = lv_label_create(parent);
  lv_label_set_text(label, value); lv_obj_set_pos(label, x, y);
  lv_obj_set_style_text_color(label, lv_color_white(), 0);
}
void run_scene(const Profile &p, const char *scene, unsigned repeat)
{
  const int width = lcd.width(), height = lcd.height();
  auto *screen = lv_screen_active();
  lv_obj_clean(screen);
  lv_obj_set_style_bg_color(screen, lv_color_hex(0x182131), 0);
  lv_obj_remove_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
  auto *pages = box(screen, 0, 0, width * 2, height, 0x182131);
  for (int page = 0; page < 2; ++page) {
    text(pages, page ? "Lights and sensors" : "Home display benchmark", width * page + 12, 12);
    for (int row = 0; row < 7; ++row) {
      auto *card = box(pages, width * page + 12, 46 + row * 52, width - 24, 48, 0x253449);
      char title[48]; snprintf(title, sizeof title, "Room %d - ceiling light", page * 7 + row + 1);
      text(card, title, 12, 3); text(card, "On    Brightness 65%", 12, 25);
    }
  }
  auto *badge = box(screen, 18, 145, 64, 64, 0x267968);
  text(badge, "65%", 12, 22);
  const bool raw = strcmp(scene, "raw-full") == 0;
  const bool full = strcmp(scene, "full-redraw") == 0;
  if (raw || full) lv_obj_add_flag(badge, LV_OBJ_FLAG_HIDDEN);
  uint32_t times[Samples]; uint64_t total = 0, io = 0, px = 0, calls = 0;
  for (unsigned i = 0; i < Warmup + Samples; ++i) {
    // Changes/layout invalidation are identical for every profile.
    if (full) { lv_obj_set_x(pages, -int((i * 7) % width)); lv_obj_invalidate(screen); }
    else if (!raw) lv_obj_set_pos(badge, 18 + (i * 7) % (width - 100), 145 + (i * 11) % 160);
    reset_counters();
    uint32_t start = micros();
    if (raw) {
      // Zeroes are invariant under byte swapping, isolating the transfer cost.
      memset(buffer, 0, width * p.rows * 2);
      for (int y = 0; y < height; y += p.rows) {
        lv_area_t area = {0, y, width - 1, std::min(height, y + int(p.rows)) - 1};
        uint32_t transfer_start = micros();
        pro_display_write(lcd, area, buffer, bulk_dma);
        io_us += uint32_t(micros() - transfer_start);
        pixels += lv_area_get_size(&area); ++flushes;
      }
    } else lv_refr_now(display);
    uint32_t elapsed = micros() - start;
    if (i >= Warmup) {
      times[i - Warmup] = elapsed; total += elapsed; io += io_us; px += pixels; calls += flushes;
    }
    delay(1); // Outside timing; let the watchdog and USB task run.
  }
  report(p, scene, repeat, times, total, io, px, calls);
}
void benchmark()
{
  Serial.println("BENCH_META,board=color-kit-pro,bus=parallel16,clock_hz=40000000,resolution=320x480,network=off");
  Serial.println("BENCH_HEADER,profile,scene,repeat,frames,mean_us,p50_us,p95_us,max_us,io_mean_us,flushes_mean,pixels_mean");
  lv_sysmon_hide_performance(display);
  for (unsigned repeat = 0; repeat < Repeats; ++repeat) {
    for (unsigned n = 0; n < sizeof(profiles) / sizeof(profiles[0]); ++n) {
      // Rotate profile order so the same candidate is not always measured last.
      const Profile &p = profiles[(n + repeat) % (sizeof(profiles) / sizeof(profiles[0]))];
      size_t bytes = lcd.width() * p.rows * 2;
      auto *next = static_cast<uint8_t *>(heap_caps_malloc(bytes, p.internal
          ? MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA | MALLOC_CAP_8BIT
          : MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
      if (!next) { Serial.printf("BENCH_SKIP,%s,allocation_failed\n", p.name); continue; }
      lv_display_set_buffers(display, next, nullptr, bytes, LV_DISPLAY_RENDER_MODE_PARTIAL);
      heap_caps_free(buffer); buffer = next; bulk_dma = p.dma; buffer_internal = p.internal;
      Serial.printf("BENCH_MEMORY,%s,buffer_bytes=%u,internal_free=%u,internal_largest=%u\n", p.name,
          unsigned(bytes), unsigned(heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)),
          unsigned(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)));
      for (const char *scene : {"raw-full", "full-redraw", "partial-update"}) run_scene(p, scene, repeat);
    }
  }
  if (buffer) {
    bulk_dma = buffer_internal; // Leave a visible colour check of the candidate path.
    auto *screen = lv_screen_active(); lv_obj_clean(screen);
    text(screen, "Benchmark complete", 20, 25);
    text(screen, bulk_dma ? "DMA: red / green / blue" : "Baseline colour check", 20, 70);
    box(screen, 20, 110, 80, 80, 0xff0000);
    box(screen, 120, 110, 80, 80, 0x00ff00);
    box(screen, 220, 110, 80, 80, 0x0000ff);
    text(screen, "Send r over USB to repeat", 20, 220);
    lv_refr_now(display);
  }
  Serial.println("BENCH_DONE"); Serial.flush();
}
}
void setup()
{
  Serial.begin(115200); delay(5000);
  lcd.init(); lcd.setRotation(0); lcd.setColorDepth(16); lcd.setBrightness(180);
  lv_init(); lv_tick_set_cb(tick);
  display = lv_display_create(lcd.width(), lcd.height());
  lv_display_set_color_format(display, LV_COLOR_FORMAT_RGB565);
  lv_display_set_flush_cb(display, flush);
  benchmark();
}
void loop()
{
  if (Serial.available() && Serial.read() == 'r') benchmark();
  delay(10);
}
#endif
