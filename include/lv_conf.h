/**
 * @file lv_conf.h
 * Minimal LVGL v9 configuration
 */

#if 1  /* Set to "1" to enable */

#ifndef LV_CONF_H
#define LV_CONF_H

#include <stdint.h>

/*====================
   COLOR SETTINGS
 *====================*/
#define LV_COLOR_DEPTH 16

/*====================
   MEMORY SETTINGS
 *====================*/
#define LV_MEM_SIZE (64 * 1024U)  /* [bytes] */

/*====================
   FONT USAGE
 *====================*/
#define LV_FONT_MONTSERRAT_14  1
#define LV_FONT_DEFAULT        &lv_font_montserrat_14

/*====================
   WIDGET USAGE
 *====================*/
#define LV_USE_BUTTON  1
#define LV_USE_LABEL   1

#endif /* LV_CONF_H */
#endif /* End of "Content enable" */
