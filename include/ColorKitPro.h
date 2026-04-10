#pragma once

#define LGFX_USE_V1

#include <LovyanGFX.hpp>
#include <driver/i2c.h>
#include "settings.h"

class ColorKitPro : public lgfx::LGFX_Device
{
  static constexpr int I2C_PORT_NUM = I2C_NUM_0;
  static constexpr int I2C_PIN_SDA = PIN_I2C_SDA;
  static constexpr int I2C_PIN_SCL = PIN_I2C_SCL;
  static constexpr int I2C_PIN_INT = PIN_TOUCH_INT;

  lgfx::Bus_Parallel16 _bus_instance;
  lgfx::Panel_ILI9488 _panel_instance;
  lgfx::Light_PWM _light_instance;
  lgfx::ITouch *_touch_instance_ptr = nullptr;

  // Detect and configure the touch panel once during init.
  bool init_impl(bool use_reset, bool use_clear) override
  {
    if (_touch_instance_ptr == nullptr)
    {
      lgfx::ITouch::config_t cfg;
      lgfx::i2c::init(I2C_PORT_NUM, I2C_PIN_SDA, I2C_PIN_SCL);
      if (lgfx::i2c::beginTransaction(I2C_PORT_NUM, 0x38, 400000, false).has_value()
       && lgfx::i2c::endTransaction(I2C_PORT_NUM).has_value())
      {
        _touch_instance_ptr = new lgfx::Touch_FT5x06();
        cfg = _touch_instance_ptr->config();
        cfg.i2c_addr = 0x38;
        cfg.x_max = 320;
        cfg.y_max = 480;
      }
      else if (lgfx::i2c::beginTransaction(I2C_PORT_NUM, 0x48, 400000, false).has_value()
            && lgfx::i2c::endTransaction(I2C_PORT_NUM).has_value())
      {
        _touch_instance_ptr = new lgfx::Touch_NS2009();
        cfg = _touch_instance_ptr->config();
        cfg.i2c_addr = 0x48;
        cfg.x_min = 368;
        cfg.y_min = 212;
        cfg.x_max = 3800;
        cfg.y_max = 3800;
      }
      if (_touch_instance_ptr != nullptr)
      {
        cfg.i2c_port = I2C_PORT_NUM;
        cfg.pin_sda = I2C_PIN_SDA;
        cfg.pin_scl = I2C_PIN_SCL;
        cfg.pin_int = I2C_PIN_INT;
        cfg.freq = 400000;
        cfg.bus_shared = false;
        _touch_instance_ptr->config(cfg);
        _panel_instance.touch(_touch_instance_ptr);
      }
    }
    return lgfx::LGFX_Device::init_impl(use_reset, use_clear);
  }

public:
  ColorKitPro(void)
  {
    {
      auto cfg = _bus_instance.config();

      cfg.freq_write = 40000000;
      cfg.pin_wr = PIN_TFT_WR;
      cfg.pin_rd = PIN_TFT_RD;
      cfg.pin_rs = PIN_TFT_RS;

      cfg.pin_d0 = PIN_DB0;
      cfg.pin_d1 = PIN_DB1;
      cfg.pin_d2 = PIN_DB2;
      cfg.pin_d3 = PIN_DB3;
      cfg.pin_d4 = PIN_DB4;
      cfg.pin_d5 = PIN_DB5;
      cfg.pin_d6 = PIN_DB6;
      cfg.pin_d7 = PIN_DB7;
      cfg.pin_d8 = PIN_DB8;
      cfg.pin_d9 = PIN_DB9;
      cfg.pin_d10 = PIN_DB10;
      cfg.pin_d11 = PIN_DB11;
      cfg.pin_d12 = PIN_DB12;
      cfg.pin_d13 = PIN_DB13;
      cfg.pin_d14 = PIN_DB14;
      cfg.pin_d15 = PIN_DB15;
      _bus_instance.config(cfg);
      _panel_instance.bus(&_bus_instance);
    }

    {
      auto cfg = _panel_instance.config();
      cfg.pin_cs = PIN_TFT_CS;
      cfg.pin_rst = PIN_TFT_RESET;
      cfg.pin_busy = -1;
      cfg.offset_rotation = 2;
      cfg.readable = true;
      cfg.invert = false;
      cfg.rgb_order = false;
      cfg.dlen_16bit = true;
      cfg.bus_shared = true;

      _panel_instance.config(cfg);
    }

    {
      auto cfg = _light_instance.config();

      cfg.pin_bl = PIN_TFT_LED;
      cfg.invert = false;
      cfg.freq = 44100;
      cfg.pwm_channel = 7;

      _light_instance.config(cfg);
      _panel_instance.light(&_light_instance);
    }
    setPanel(&_panel_instance);
  }
};