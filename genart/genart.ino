// Generative art for the Waveshare ESP32-S3-LCD-1.47B (ST7789, 172x320).
//
// CPU-rendered framebuffer effects pushed to the panel over DMA. Uses an 8-bit
// PALETTED sprite (1 byte/pixel): effects write a 0..255 index per pixel and the
// palette maps index->RGB565, so we get cheap per-pixel math AND a free per-effect
// recolor just by swapping the palette. ~55 KB framebuffer in internal SRAM.
//
// Controls:
//   BOOT button (GPIO0, active LOW) -> cycle effect (also re-colors via palette).
//   RGB LED  (GPIO38)               -> reflects the current effect.
//
// Backlight is GPIO46 on the 1.47B (NOT 48 — that's the base 1.47). Driven HIGH
// directly; LovyanGFX Light_PWM's LEDC path is broken on esp32 core 3.x. See CLAUDE.md.

#define LGFX_USE_V1
#include <LovyanGFX.hpp>
#include <math.h>

#define BL_PIN  46   // 1.47B backlight, active HIGH (defaults off via gate pulldown)
#define BTN_PIN 0    // BOOT button, active LOW, needs INPUT_PULLUP
#define RGB_PIN 38   // onboard WS2812

// --- Verified panel config for this board (identical to display_test) ----------
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
LGFX lcd;

// --- Framebuffer ----------------------------------------------------------------
static const int W = 172;   // divisible by 4 -> no row padding for an 8bpp sprite
static const int H = 320;
LGFX_Sprite fb(&lcd);
uint8_t* buf = nullptr;     // raw paletted pixels, row-major, stride == W

// --- Lookup tables --------------------------------------------------------------
uint8_t sinLUT[256];        // sin scaled to 0..255 (centered at 128)

static const int NUM_EFFECTS = 3;
uint8_t palettes[NUM_EFFECTS][256][3];   // [effect][index] -> {r,g,b}
int effect = 0;

static inline uint8_t clamp8(int v) { return v < 0 ? 0 : (v > 255 ? 255 : v); }

void hsv2rgb(uint8_t h, uint8_t s, uint8_t v, uint8_t& r, uint8_t& g, uint8_t& b) {
  uint8_t region = h / 43;
  uint8_t rem = (h - region * 43) * 6;
  uint8_t p = (v * (255 - s)) >> 8;
  uint8_t q = (v * (255 - ((s * rem) >> 8))) >> 8;
  uint8_t t = (v * (255 - ((s * (255 - rem)) >> 8))) >> 8;
  switch (region) {
    case 0:  r = v; g = t; b = p; break;
    case 1:  r = q; g = v; b = p; break;
    case 2:  r = p; g = v; b = t; break;
    case 3:  r = p; g = q; b = v; break;
    case 4:  r = t; g = p; b = v; break;
    default: r = v; g = p; b = q; break;
  }
}

void buildTables() {
  for (int i = 0; i < 256; i++)
    sinLUT[i] = (uint8_t)(sinf(i * 2.0f * (float)M_PI / 256.0f) * 127.0f + 128.0f);

  for (int i = 0; i < 256; i++) {
    // 0: rainbow (full HSV sweep)
    hsv2rgb((uint8_t)i, 255, 255,
            palettes[0][i][0], palettes[0][i][1], palettes[0][i][2]);
    // 1: fire (black -> red -> yellow -> white)
    palettes[1][i][0] = i < 85 ? i * 3 : 255;
    palettes[1][i][1] = i < 85 ? 0 : (i < 170 ? (i - 85) * 3 : 255);
    palettes[1][i][2] = i < 170 ? 0 : clamp8((i - 170) * 3);
    // 2: ocean (black -> blue -> cyan -> white)
    palettes[2][i][2] = i < 85 ? i * 3 : 255;
    palettes[2][i][1] = i < 85 ? 0 : (i < 170 ? (i - 85) * 3 : 255);
    palettes[2][i][0] = i < 170 ? 0 : clamp8((i - 170) * 3);
  }
}

void applyPalette(int e) {
  for (int i = 0; i < 256; i++)
    fb.setPaletteColor(i, palettes[e][i][0], palettes[e][i][1], palettes[e][i][2]);
}

void showLed(int e) {
  switch (e) {
    case 0:  rgbLedWrite(RGB_PIN, 0, 30, 0); break;   // (LED is GRB-ordered; cosmetic)
    case 1:  rgbLedWrite(RGB_PIN, 30, 0, 0); break;
    default: rgbLedWrite(RGB_PIN, 0, 0, 30); break;
  }
}

// --- Effects: fill buf[] with palette indices -----------------------------------
void fxPlasma(uint32_t f) {
  uint8_t t = (uint8_t)f;
  for (int y = 0; y < H; y++) {
    int yo = y * W;
    for (int x = 0; x < W; x++) {
      int v = sinLUT[(uint8_t)(x * 2 + t)]
            + sinLUT[(uint8_t)(y * 2 + t)]
            + sinLUT[(uint8_t)((x + y) + (t << 1))]
            + sinLUT[(uint8_t)(((x * x + y * y) >> 5) + t)];
      buf[yo + x] = (uint8_t)(v >> 2);
    }
  }
}

void fxRings(uint32_t f) {
  uint8_t t = (uint8_t)f;
  for (int y = 0; y < H; y++) {
    int yo = y * W; int dy = y - H / 2;
    for (int x = 0; x < W; x++) {
      int dx = x - W / 2;
      uint32_t d2 = (uint32_t)(dx * dx + dy * dy);
      buf[yo + x] = sinLUT[(uint8_t)((d2 >> 6) - t)];
    }
  }
}

void fxWeave(uint32_t f) {
  uint8_t t = (uint8_t)f;
  for (int y = 0; y < H; y++) {
    int yo = y * W;
    for (int x = 0; x < W; x++) {
      int v = sinLUT[(uint8_t)(x * 3 + t)]
            + sinLUT[(uint8_t)(y * 3 - (t >> 1))]
            + sinLUT[(uint8_t)((x - y) * 2 + t)];
      buf[yo + x] = (uint8_t)((v * 85) >> 8);   // /3, scaled to 0..255
    }
  }
}

void render(uint32_t f) {
  switch (effect) {
    case 0:  fxPlasma(f); break;
    case 1:  fxRings(f);  break;
    default: fxWeave(f);  break;
  }
  fb.pushSprite(0, 0);
}

// --- Button (debounced falling edge) --------------------------------------------
bool lastBtn = HIGH;
uint32_t lastBtnMs = 0;
void checkButton() {
  bool b = digitalRead(BTN_PIN);
  if (b != lastBtn && (millis() - lastBtnMs) > 40) {
    lastBtnMs = millis();
    lastBtn = b;
    if (b == LOW) {            // pressed
      effect = (effect + 1) % NUM_EFFECTS;
      applyPalette(effect);
      showLed(effect);
      Serial.printf("[genart] effect -> %d\n", effect);
    }
  }
}

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println("\n[genart] booting");

  bool ok = lcd.init();
  lcd.setRotation(0);
  pinMode(BL_PIN, OUTPUT);
  digitalWrite(BL_PIN, HIGH);          // backlight on (GPIO46)
  Serial.printf("[genart] init=%s  %dx%d\n", ok ? "true" : "false", lcd.width(), lcd.height());

  buildTables();
  fb.setColorDepth(lgfx::palette_8bit);
  buf = (uint8_t*)fb.createSprite(W, H);
  Serial.printf("[genart] sprite buf=%s\n", buf ? "OK" : "NULL");
  applyPalette(effect);

  pinMode(BTN_PIN, INPUT_PULLUP);
  showLed(effect);
  Serial.println("[genart] running — press BOOT to cycle effects");
}

void loop() {
  static uint32_t frame = 0;
  static uint32_t t0 = 0, fps_n = 0;
  checkButton();
  render(frame);
  frame++;
  fps_n++;
  if (millis() - t0 > 1000) {
    Serial.printf("[genart] effect=%d  fps=%lu\n", effect, (unsigned long)fps_n);
    fps_n = 0; t0 = millis();
  }
}
