// board.h — single source of truth for the Waveshare ESP32-S3-LCD-1.47B hardware.
// Include this in any sketch for this board. Defines pins, the verified LovyanGFX
// panel config, and the screen dimensions. (Does NOT define any globals — declare
// `LGFX lcd;` in your .ino.)
//
// KEY GOTCHA: backlight is GPIO46 on the 1.47B (the base 1.47 uses 48). It defaults
// OFF via a 10K gate pulldown, so drive PIN_BL HIGH after init. LovyanGFX Light_PWM
// is intentionally NOT used (its LEDC path is broken on esp32 core 3.x). See CLAUDE.md.
#pragma once

#define LGFX_USE_V1
#include <LovyanGFX.hpp>

// --- Pins (1.47B, from the official schematic in docs/) -------------------------
#define PIN_BL   46   // LCD backlight, active HIGH
#define PIN_BTN  0    // BOOT button, active LOW (use INPUT_PULLUP)
#define PIN_RGB  38   // onboard WS2812 RGB LED

// microSD on the SDMMC bus (4-bit). Used by the video player; no conflict with the
// LCD's SPI3 pins (40/45/42/41/39) or BL/RGB/BTN.
#define PIN_SD_CLK 14
#define PIN_SD_CMD 15
#define PIN_SD_D0  16
#define PIN_SD_D1  18
#define PIN_SD_D2  17
#define PIN_SD_D3  21

// --- Screen ---------------------------------------------------------------------
// 172 is divisible by 4, so an 8bpp sprite has stride == width (no row padding).
static const int SCREEN_W = 172;
static const int SCREEN_H = 320;

// --- Verified ST7789 panel config ------------------------------------------------
class LGFX : public lgfx::LGFX_Device {
  lgfx::Panel_ST7789 _panel;
  lgfx::Bus_SPI      _bus;
public:
  LGFX() {
    { auto cfg = _bus.config();
      cfg.spi_host = SPI3_HOST; cfg.spi_mode = 0;
      cfg.freq_write = 80000000; cfg.freq_read = 16000000;
      cfg.spi_3wire = false; cfg.use_lock = true;
      cfg.dma_channel = SPI_DMA_CH_AUTO;
      cfg.pin_sclk = 40; cfg.pin_mosi = 45; cfg.pin_miso = -1; cfg.pin_dc = 41;
      _bus.config(cfg); _panel.setBus(&_bus); }
    { auto cfg = _panel.config();
      cfg.pin_cs = 42; cfg.pin_rst = 39; cfg.pin_busy = -1;
      cfg.memory_width = 320; cfg.memory_height = 172;
      cfg.panel_width = 172;  cfg.panel_height = 320;
      cfg.offset_x = 34; cfg.offset_y = 0; cfg.offset_rotation = 0;
      cfg.dummy_read_pixel = 8; cfg.dummy_read_bits = 1; cfg.readable = false;
      cfg.invert = true; cfg.rgb_order = false; cfg.dlen_16bit = false;
      cfg.bus_shared = false;
      _panel.config(cfg); }
    setPanel(&_panel);
  }
};
