#include "ui/ReleaseNotes.h"
#include "ui/I18n.h"

namespace release_notes {
std::string url() {
    return "https://github.com/ThingPulse/esp32-gadget-os/blob/master/docs/releases/since-v0.1.3.en.md";
}

lv_obj_t *create_qr(lv_obj_t *parent) {
    auto *frame = lv_obj_create(parent);
    lv_obj_set_size(frame,208,208);
    lv_obj_set_style_bg_color(frame,lv_color_white(),0);
    lv_obj_set_style_bg_opa(frame,LV_OPA_COVER,0);
    lv_obj_set_style_border_width(frame,0,0);
    lv_obj_set_style_radius(frame,0,0);
    lv_obj_set_style_pad_all(frame,16,0);
    lv_obj_remove_flag(frame,LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_align(frame,LV_ALIGN_CENTER,0);
#if LV_USE_QRCODE
    auto *qr = lv_qrcode_create(frame);
    lv_qrcode_set_size(qr,176);
    lv_qrcode_set_dark_color(qr,lv_color_black());
    lv_qrcode_set_light_color(qr,lv_color_white());
    lv_obj_center(qr);
    const auto link = url();
    if (lv_qrcode_update(qr,link.data(),link.size()) == LV_RESULT_OK) return frame;
    lv_obj_delete(qr);
#endif
    auto *fallback = lv_label_create(frame);
    lv_obj_set_width(fallback,176);
    lv_label_set_text(fallback,url().c_str());
    lv_label_set_long_mode(fallback,LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_color(fallback,lv_color_black(),0);
    lv_obj_set_style_text_font(fallback,i18n::font14(),0);
    lv_obj_center(fallback);
    return frame;
}
}
