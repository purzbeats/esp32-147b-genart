// Display validation sketch for Waveshare ESP32-S3-LCD-1.47
// ST7789, 172x320 IPS. Verifies controller, color order, rotation, and pixel offset.
//
// Pins (from board docs): SCLK=40 MOSI=45 CS=42 DC=41 RST=39 BL=48
// If colors look swapped or the image is shifted/mirrored, tweak the
// CONFIG markers below and reflash — see notes in chat.

#define LGFX_USE_V1
#include <LovyanGFX.hpp>

#define BL_PIN 48   // backlight, active HIGH (confirmed by Waveshare demo)

// Config replicated from a confirmed-working LovyanGFX project for this exact
// board (ahmadrezarazian/Waveshare_ESP32-S3-LCD1.47_3D-Box). Key fix vs my
// first attempt: spi_3wire=false (this panel is wired 4-wire with a real DC pin).
class LGFX : public lgfx::LGFX_Device {
  lgfx::Panel_ST7789  _panel;
  lgfx::Bus_SPI       _bus;
  lgfx::Light_PWM     _light;

public:
  LGFX() {
    { // SPI bus
      auto cfg = _bus.config();
      cfg.spi_host    = SPI3_HOST;
      cfg.spi_mode    = 0;
      cfg.freq_write  = 80000000;   // 80 MHz write clock
      cfg.freq_read   = 16000000;
      cfg.spi_3wire   = false;      // 4-wire SPI with dedicated DC pin (was the bug)
      cfg.use_lock    = true;
      cfg.dma_channel = SPI_DMA_CH_AUTO;
      cfg.pin_sclk    = 40;
      cfg.pin_mosi    = 45;
      cfg.pin_miso    = -1;
      cfg.pin_dc      = 41;
      _bus.config(cfg);
      _panel.setBus(&_bus);
    }
    { // Panel
      auto cfg = _panel.config();
      cfg.pin_cs           = 42;
      cfg.pin_rst          = 39;
      cfg.pin_busy         = -1;
      cfg.memory_width     = 320;
      cfg.memory_height    = 172;
      cfg.panel_width      = 172;
      cfg.panel_height     = 320;
      cfg.offset_x         = 34;    // 172-wide panel centered in controller RAM
      cfg.offset_y         = 0;
      cfg.offset_rotation  = 0;
      cfg.dummy_read_pixel = 8;
      cfg.dummy_read_bits  = 1;
      cfg.readable         = false;
      cfg.invert           = true;
      cfg.rgb_order        = false; // RGB (proven value; flip if red<->blue swap)
      cfg.dlen_16bit       = false;
      cfg.bus_shared       = false;
      _panel.config(cfg);
    }
    { // Backlight (PWM on GPIO48, active HIGH)
      auto cfg = _light.config();
      cfg.pin_bl      = 48;
      cfg.invert      = false;
      cfg.freq        = 44100;
      cfg.pwm_channel = 7;
      _light.config(cfg);
      _panel.setLight(&_light);
    }
    setPanel(&_panel);
  }
};

LGFX lcd;

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println("\n[display_test] booting");

  lcd.init();
  lcd.setRotation(0);   // 0/1 = portrait 172x320, 2/3 = landscape 320x172
  lcd.setBrightness(200);

  Serial.printf("[display_test] panel %d x %d\n", lcd.width(), lcd.height());
  Serial.println("[display_test] entering continuous draw loop");
}

// Draw the full test pattern. Called every frame so a first-frame loss during
// panel power-up can't leave us stuck on black.
void drawPattern(uint32_t frame) {
  int w = lcd.width();
  int h = lcd.height();

  // Horizontal color bars across full height (any vertical offset is obvious).
  uint16_t bars[] = { TFT_RED, TFT_GREEN, TFT_BLUE, TFT_YELLOW,
                      TFT_CYAN, TFT_MAGENTA, TFT_WHITE, TFT_BLACK };
  int n = sizeof(bars) / sizeof(bars[0]);
  int bh = h / n;
  for (int i = 0; i < n; i++) {
    lcd.fillRect(0, i * bh, w, bh, bars[i]);
  }

  // 1px white frame: if any edge is missing/clipped, the offset is wrong.
  lcd.drawRect(0, 0, w, h, TFT_WHITE);

  // Corner markers to check orientation/mirroring.
  lcd.fillCircle(8, 8, 6, TFT_WHITE);                      // top-left dot
  lcd.fillTriangle(w-1, 0, w-14, 0, w-1, 14, TFT_BLACK);   // top-right notch

  // Live frame counter: if this number climbs, the display is genuinely working.
  lcd.setTextColor(TFT_BLACK, TFT_WHITE);
  lcd.setTextSize(2);
  lcd.setCursor(10, h / 2 - 16);
  lcd.print("HELLO S3");
  lcd.setTextSize(1);
  lcd.setCursor(10, h / 2 + 8);
  lcd.printf("%dx%d  f=%lu", w, h, (unsigned long)frame);

  // A bar that sweeps down the screen so motion is unmistakable if it's alive.
  int y = (frame * 4) % h;
  lcd.fillRect(0, y, w, 3, TFT_WHITE);
}

void loop() {
  static uint32_t frame = 0;
  static uint32_t t = 0;
  drawPattern(frame);
  frame++;
  if (millis() - t > 1000) {
    t = millis();
    Serial.printf("[display_test] alive, frame=%lu\n", (unsigned long)frame);
  }
  delay(30);
}
