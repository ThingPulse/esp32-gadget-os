#pragma once

#include <LovyanGFX.hpp>
#include <lvgl.h>
#include <src/draw/sw/lv_draw_sw_utils.h>

// Only for the Pro's RGB565 parallel panel and internal DMA-capable buffers.
// LovyanGFX's parallel bus expects swapped RGB565 (its GPIO mapping swaps bytes).
// Swapping the whole LVGL strip avoids conversion through 256-byte staging buffers.
// Keep the transaction synchronous: LVGL may reuse the buffer only after endWrite.
inline void pro_display_write(lgfx::LGFX_Device &lcd, const lv_area_t &area,
                              uint8_t *pixels, bool bulk_dma)
{
  const int32_t width = lv_area_get_width(&area);
  const int32_t height = lv_area_get_height(&area);
  const int32_t count = width * height;
  lcd.startWrite();
  lcd.setAddrWindow(area.x1, area.y1, width, height);
  if (bulk_dma) {
    lv_draw_sw_rgb565_swap(pixels, count);
    lcd.writePixelsDMA(reinterpret_cast<lgfx::swap565_t *>(pixels), count);
  } else {
    lcd.writePixels(reinterpret_cast<lgfx::rgb565_t *>(pixels), count);
  }
  lcd.endWrite();
}
