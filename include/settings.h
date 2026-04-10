#pragma once

// Pin definitions for ColorKitPro. Override via build_flags in platformio.ini.

#if !defined(PIN_TFT_WR)
#define PIN_TFT_WR -1
#endif

#if !defined(PIN_TFT_RD)
#define PIN_TFT_RD -1
#endif

#if !defined(PIN_TFT_RS)
#define PIN_TFT_RS -1
#endif

#if !defined(PIN_TFT_CS)
#if defined(TFT_CS)
#define PIN_TFT_CS TFT_CS
#else
#define PIN_TFT_CS -1
#endif
#endif

#if !defined(PIN_TFT_RESET)
#if defined(TFT_RST)
#define PIN_TFT_RESET TFT_RST
#else
#define PIN_TFT_RESET -1
#endif
#endif

#if !defined(PIN_TFT_LED)
#if defined(TFT_BL)
#define PIN_TFT_LED TFT_BL
#else
#define PIN_TFT_LED -1
#endif
#endif

#if !defined(PIN_DB0)
#define PIN_DB0 -1
#endif
#if !defined(PIN_DB1)
#define PIN_DB1 -1
#endif
#if !defined(PIN_DB2)
#define PIN_DB2 -1
#endif
#if !defined(PIN_DB3)
#define PIN_DB3 -1
#endif
#if !defined(PIN_DB4)
#define PIN_DB4 -1
#endif
#if !defined(PIN_DB5)
#define PIN_DB5 -1
#endif
#if !defined(PIN_DB6)
#define PIN_DB6 -1
#endif
#if !defined(PIN_DB7)
#define PIN_DB7 -1
#endif
#if !defined(PIN_DB8)
#define PIN_DB8 -1
#endif
#if !defined(PIN_DB9)
#define PIN_DB9 -1
#endif
#if !defined(PIN_DB10)
#define PIN_DB10 -1
#endif
#if !defined(PIN_DB11)
#define PIN_DB11 -1
#endif
#if !defined(PIN_DB12)
#define PIN_DB12 -1
#endif
#if !defined(PIN_DB13)
#define PIN_DB13 -1
#endif
#if !defined(PIN_DB14)
#define PIN_DB14 -1
#endif
#if !defined(PIN_DB15)
#define PIN_DB15 -1
#endif

#if !defined(PIN_I2C_SDA)
#define PIN_I2C_SDA 40
#endif

#if !defined(PIN_I2C_SCL)
#define PIN_I2C_SCL 41
#endif

#if !defined(PIN_TOUCH_INT)
#define PIN_TOUCH_INT 6
#endif