#ifndef DISPLAY_H
#define DISPLAY_H

#include <LovyanGFX.hpp>

class LGFX : public lgfx::LGFX_Device {
public:
  lgfx::Panel_ST7735S _panel_instance;
  lgfx::Bus_SPI _bus_instance;

  LGFX(void) {
    {
      auto cfg = _bus_instance.config();
      cfg.spi_host = SPI2_HOST;
      cfg.spi_mode = 0;
      cfg.freq_write = 40000000;
      cfg.freq_read = 16000000;
      cfg.pin_sclk = 4;
      cfg.pin_mosi = 3;
      cfg.pin_miso = -1;
      cfg.pin_dc   = 1;
      _bus_instance.config(cfg);
      _panel_instance.setBus(&_bus_instance);
    }

    {
      auto cfg = _panel_instance.config();
      cfg.pin_cs = 0;
      cfg.pin_rst = 2;
      cfg.pin_busy = -1;
      cfg.memory_width = 128;
      cfg.memory_height = 160;
      cfg.panel_width = 128;
      cfg.panel_height = 128;
      cfg.offset_x = 0;
      cfg.offset_y = 0;
      cfg.rgb_order = false;
      cfg.dummy_read_pixel = 8;
      cfg.dummy_read_bits = 1;
      cfg.readable = false;
      cfg.invert = false;
      cfg.bus_shared = false;
      _panel_instance.config(cfg);
    }

    setPanel(&_panel_instance);
  }
};

extern LGFX tft;

void clearRect(int x, int y, int w, int h);
void drawBg();

#endif
