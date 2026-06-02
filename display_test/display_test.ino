// Display validation sketch for Waveshare ESP32-S3-LCD-1.47
// ST7789, 172x320 IPS. Verifies controller, color order, rotation, and pixel offset.
//
// Pins (from board docs): SCLK=40 MOSI=45 CS=42 DC=41 RST=39 BL=48
// If colors look swapped or the image is shifted/mirrored, tweak the
// CONFIG markers below and reflash — see notes in chat.

#define LGFX_USE_V1
#include <LovyanGFX.hpp>

#define BL_PIN 46   // 1.47B backlight (active HIGH). NOTE: the base 1.47 uses GPIO48;
                    // the *B* revision moved it to 46 (per the official 1.47B schematic).
                    // Defaults OFF via a 10K gate pulldown, so it must be driven HIGH.
// Backlight is driven directly (not via LovyanGFX Light_PWM) — see note on the LGFX class.

// Config replicated from a confirmed-working LovyanGFX project for this exact
// board (ahmadrezarazian/Waveshare_ESP32-S3-LCD1.47_3D-Box). Key fix vs my
// first attempt: spi_3wire=false (this panel is wired 4-wire with a real DC pin).
// NOTE (2026-06-01): the backlight is NOT managed by LovyanGFX here. Its Light_PWM
// uses the LEDC API, which is broken on esp32 Arduino core 3.x (we're on 3.3.8) —
// see LovyanGFX issue #708. That failure leaves the backlight off, which is the
// real cause of the "init OK but black screen" we chased for days. We drive GPIO48
// directly with digitalWrite(HIGH) in setup() instead. (No PWM dimming for now;
// can be restored later via the core-3.x ledcAttach/ledcWrite API if needed.)
class LGFX : public lgfx::LGFX_Device {
  lgfx::Panel_ST7789  _panel;
  lgfx::Bus_SPI       _bus;

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
    // Backlight (GPIO48) is driven manually in setup() — see note above the class.
    setPanel(&_panel);
  }
};

LGFX lcd;

// Full-screen framebuffer sprite. This mirrors the ONLY proven-working code path
// for this exact board (the 3D-Box reference repo draws into a sprite and pushes
// it, rather than issuing primitives straight to the panel). 172x320x2B = ~110 KB,
// allocated from internal SRAM (PSRAM disabled), well within the S3's 512 KB.
LGFX_Sprite fb(&lcd);

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println("\n[display_test] booting");

  bool ok = lcd.init();   // returns false if the bus/panel setup failed
  Serial.printf("[display_test] lcd.init() -> %s\n", ok ? "true" : "false");

  lcd.setRotation(0);     // 0/1 = portrait 172x320, 2/3 = landscape 320x172

  // Drive the backlight ON directly. LovyanGFX's Light_PWM (LEDC) is broken on
  // esp32 core 3.x and was leaving this off -> black screen. Plain digitalWrite
  // sidesteps LEDC entirely.
  pinMode(BL_PIN, OUTPUT);
  digitalWrite(BL_PIN, HIGH);
  Serial.printf("[display_test] backlight GPIO%d driven HIGH (direct, no PWM)\n", BL_PIN);

  int w = lcd.width();
  int h = lcd.height();
  Serial.printf("[display_test] panel %d x %d\n", w, h);

  // Allocate the framebuffer sprite and report whether it actually got memory.
  void* buf = fb.createSprite(w, h);
  Serial.printf("[display_test] createSprite(%d,%d) -> %s\n",
                w, h, buf ? "OK" : "FAILED (null)");

  Serial.println("[display_test] entering continuous draw loop");
}

// Render the full test pattern into the sprite, then DMA the whole sprite to the
// panel in one push. Done every frame so a first-frame loss during power-up can't
// leave us stuck on black.
void drawPattern(uint32_t frame) {
  int w = fb.width();
  int h = fb.height();

  // Horizontal color bars across full height (any vertical offset is obvious).
  uint16_t bars[] = { TFT_RED, TFT_GREEN, TFT_BLUE, TFT_YELLOW,
                      TFT_CYAN, TFT_MAGENTA, TFT_WHITE, TFT_BLACK };
  int n = sizeof(bars) / sizeof(bars[0]);
  int bh = h / n;
  for (int i = 0; i < n; i++) {
    fb.fillRect(0, i * bh, w, bh, bars[i]);
  }

  // 1px white frame: if any edge is missing/clipped, the offset is wrong.
  fb.drawRect(0, 0, w, h, TFT_WHITE);

  // Corner markers to check orientation/mirroring.
  fb.fillCircle(8, 8, 6, TFT_WHITE);                      // top-left dot
  fb.fillTriangle(w-1, 0, w-14, 0, w-1, 14, TFT_BLACK);   // top-right notch

  // Live frame counter: if this number climbs, the display is genuinely working.
  fb.setTextColor(TFT_BLACK, TFT_WHITE);
  fb.setTextSize(2);
  fb.setCursor(10, h / 2 - 16);
  fb.print("HELLO S3");
  fb.setTextSize(1);
  fb.setCursor(10, h / 2 + 8);
  fb.printf("%dx%d  f=%lu", w, h, (unsigned long)frame);

  // A bar that sweeps down the screen so motion is unmistakable if it's alive.
  int y = (frame * 4) % h;
  fb.fillRect(0, y, w, 3, TFT_WHITE);

  fb.pushSprite(0, 0);   // single DMA blit of the whole framebuffer
}

void loop() {
  static uint32_t frame = 0;
  static uint32_t t = 0;
  drawPattern(frame);
  frame++;

  if (millis() - t > 1000) {
    t = millis();
    // Debug-plan step #3: toggle hardware inversion once a second. If the panel
    // is alive but the invert setting is wrong, the user will see it flip between
    // a correct image and a photo-negative once per second; logged so the serial
    // trace lines up with whatever is on screen.
    static bool inv = true;   // matches cfg.invert = true
    inv = !inv;
    lcd.invertDisplay(inv);
    Serial.printf("[display_test] alive, frame=%lu, invertDisplay=%s\n",
                  (unsigned long)frame, inv ? "true" : "false");
  }
  delay(30);
}
