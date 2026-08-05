#include "assets/ThingPulseLogo.h"

#include "assets/ThingPulseLogoData.inc"

const lv_image_dsc_t thingpulse_logo = {
    {LV_IMAGE_HEADER_MAGIC, LV_COLOR_FORMAT_RGB565, 0, 240, 96, 480, 0},
    sizeof(thingpulse_logo_rgb565),
    thingpulse_logo_rgb565,
    nullptr,
    nullptr,
};
