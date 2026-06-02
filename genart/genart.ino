// Generative art for the Waveshare ESP32-S3-LCD-1.47B (ST7789, 172x320).
//
// Architecture (see also board.h / effects.h):
//   - board.h   : verified hardware config (pins, LGFX panel class, dimensions)
//   - effects.h : the effect "standard" — Inputs struct + Effect registry
//   - effects.cpp: sin/palette tables + effect functions + EFFECTS[] table
//   - this file : orchestration — render loop, BOOT-button effect cycling, LED, fps
//
// Render path: effects write 8-bit palette indices into a paletted LGFX_Sprite
// (1 byte/pixel, ~55 KB in internal SRAM); pushSprite() DMAs it out, converting
// index->RGB565 via the active palette. Backlight is GPIO46 (1.47B), driven HIGH.

#include "board.h"
#include "effects.h"

LGFX lcd;
LGFX_Sprite fb(&lcd);
uint8_t* buf = nullptr;
int effect = 0;

void applyPalette(int e) {
  int pid = EFFECTS[e].palette;
  for (int i = 0; i < 256; i++)
    fb.setPaletteColor(i, PALETTES[pid][i][0], PALETTES[pid][i][1], PALETTES[pid][i][2]);
}

void showLed(int e) {
  rgbLedWrite(PIN_RGB, EFFECTS[e].ledR, EFFECTS[e].ledG, EFFECTS[e].ledB);
}

// BOOT button: debounced falling edge -> next effect.
bool lastBtn = HIGH;
uint32_t lastBtnMs = 0;
void checkButton() {
  bool b = digitalRead(PIN_BTN);
  if (b != lastBtn && (millis() - lastBtnMs) > 40) {
    lastBtnMs = millis();
    lastBtn = b;
    if (b == LOW) {
      effect = (effect + 1) % NUM_EFFECTS;
      applyPalette(effect);
      showLed(effect);
      Serial.printf("[genart] effect -> %d (%s)\n", effect, EFFECTS[effect].name);
    }
  }
}

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println("\n[genart] booting");

  bool ok = lcd.init();
  lcd.setRotation(0);
  pinMode(PIN_BL, OUTPUT);
  digitalWrite(PIN_BL, HIGH);            // backlight on (GPIO46)
  Serial.printf("[genart] init=%s  %dx%d  effects=%d\n",
                ok ? "true" : "false", lcd.width(), lcd.height(), NUM_EFFECTS);

  buildTables();
  fb.setColorDepth(lgfx::palette_8bit);
  buf = (uint8_t*)fb.createSprite(SCREEN_W, SCREEN_H);
  Serial.printf("[genart] sprite buf=%s\n", buf ? "OK" : "NULL");
  applyPalette(effect);

  pinMode(PIN_BTN, INPUT_PULLUP);
  showLed(effect);
  Serial.println("[genart] running — press BOOT to cycle effects");
}

void loop() {
  static uint32_t frame = 0;
  static uint32_t t0 = 0, fps_n = 0;

  checkButton();

  Inputs in = { frame, 0.0f, 0.0f, 1.0f };   // flat until the IMU is wired up
  EFFECTS[effect].fn(buf, SCREEN_W, SCREEN_H, in);
  fb.pushSprite(0, 0);

  frame++;
  fps_n++;
  if (millis() - t0 > 1000) {
    Serial.printf("[genart] effect=%d (%s)  fps=%lu\n",
                  effect, EFFECTS[effect].name, (unsigned long)fps_n);
    fps_n = 0; t0 = millis();
  }
}
