#pragma once

#include <lvgl.h>
#include <cstring>

namespace launcher {
// Some app packages use the dashboard's dark canvas as icon fill.
// Give these launcher tiles visible colors without changing signed packages.
inline uint16_t icon_tile_color(const char *id, uint16_t background) {
    if(background == 0x1106) {
        if(std::strcmp(id, "agent-usage") == 0) return 0x8318; // Purple
        if(std::strcmp(id, "system-monitor") == 0) return 0x2af6; // Blue
        if(std::strcmp(id, "home-assistant") == 0) return 0x23ee; // Teal
        if(std::strcmp(id, "planes") == 0) return 0x2af6; // Blue
        if(std::strcmp(id, "transit") == 0) return 0x23ee; // Teal
    }
    return background;
}

// Decorative frame: the containing launcher tile owns the click handler.
inline lv_obj_t *icon_frame(lv_obj_t *tile, lv_color_t color) {
    auto *frame = lv_obj_create(tile);
    lv_obj_set_size(frame, 68, 68);
    lv_obj_align(frame, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_remove_flag(frame, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(frame, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_all(frame, 0, 0);
    lv_obj_set_style_radius(frame, 18, 0);
    lv_obj_set_style_bg_opa(frame, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(frame, color, 0);
    lv_obj_set_style_text_color(frame, lv_color_white(), 0);
    lv_obj_set_style_border_width(frame, 0, 0);
    lv_obj_set_style_shadow_width(frame, 8, 0);
    lv_obj_set_style_shadow_color(frame, lv_color_hex(0x000000), 0);
    lv_obj_set_style_shadow_opa(frame, LV_OPA_20, 0);
    return frame;
}

// Package icons are opaque RGB565; extend their corner color into the frame.
inline lv_color_t icon_background(uint16_t pixel) {
    return lv_color_make(((pixel >> 11) << 3) | (pixel >> 13),
                         (((pixel >> 5) & 63) << 2) | ((pixel >> 9) & 3),
                         ((pixel & 31) << 3) | ((pixel & 31) >> 2));
}
}
