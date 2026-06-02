// Generative art for the Waveshare ESP32-S3-LCD-1.47B (ST7789, 172x320).
//
//   - board.h    : verified hardware config (pins, LGFX panel class, dimensions)
//   - effects.h  : the effect "standard" — Inputs struct + Effect registry
//   - effects.cpp: sin/palette tables + effect functions + EFFECTS[] table
//   - this file  : orchestration — dual-core render loop, button, LED, fps
//
// DUAL-CORE PIPELINE (double buffer):
//   core 0 (renderTask) computes the next frame's palette indices into a free
//   buffer; core 1 (loop) DMA-pushes the ready buffer (index->RGB565 via palette).
//   Buffers ping-pong through two queues, so compute and transfer overlap and the
//   frame time is max(render, push) instead of their sum.

#include "board.h"
#include "effects.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>

LGFX lcd;
LGFX_Sprite spr0(&lcd), spr1(&lcd);
LGFX_Sprite* sprites[2] = { &spr0, &spr1 };
uint8_t*     bufs[2]    = { nullptr, nullptr };

volatile int   g_effect = 0;
volatile float g_ax = 0.0f, g_ay = 0.0f, g_az = 1.0f;   // tilt; flat until IMU lands
volatile uint32_t g_renderUs = 0, g_pushUs = 0;          // timing diagnostics

QueueHandle_t freeQ, readyQ;   // carry buffer indices (0/1) between the two cores

void applyPalette(LGFX_Sprite* s, int e) {
  int pid = EFFECTS[e].palette;
  for (int i = 0; i < 256; i++)
    s->setPaletteColor(i, PALETTES[pid][i][0], PALETTES[pid][i][1], PALETTES[pid][i][2]);
}

void showLed(int e) {
  rgbLedWrite(PIN_RGB, EFFECTS[e].ledR, EFFECTS[e].ledG, EFFECTS[e].ledB);
}

// Producer (core 0): take a free buffer, render into it, hand it to the consumer.
void renderTask(void*) {
  uint32_t frame = 0;
  for (;;) {
    int idx;
    xQueueReceive(freeQ, &idx, portMAX_DELAY);
    Inputs in = { frame, g_ax, g_ay, g_az };
    uint32_t t = micros();
    EFFECTS[g_effect].fn(bufs[idx], SCREEN_W, SCREEN_H, in);
    g_renderUs = micros() - t;
    frame++;
    xQueueSend(readyQ, &idx, portMAX_DELAY);
  }
}

// BOOT button: debounced falling edge -> next effect (palette on BOTH buffers).
bool lastBtn = HIGH;
uint32_t lastBtnMs = 0;
void checkButton() {
  bool b = digitalRead(PIN_BTN);
  if (b != lastBtn && (millis() - lastBtnMs) > 40) {
    lastBtnMs = millis();
    lastBtn = b;
    if (b == LOW) {
      int e = (g_effect + 1) % NUM_EFFECTS;
      applyPalette(sprites[0], e);
      applyPalette(sprites[1], e);
      g_effect = e;
      showLed(e);
      Serial.printf("[genart] effect -> %d (%s)\n", e, EFFECTS[e].name);
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
  digitalWrite(PIN_BL, HIGH);                // backlight on (GPIO46)

  buildTables();
  for (int i = 0; i < 2; i++) {
    sprites[i]->setColorDepth(lgfx::palette_8bit);
    bufs[i] = (uint8_t*)sprites[i]->createSprite(SCREEN_W, SCREEN_H);
    applyPalette(sprites[i], g_effect);
  }
  Serial.printf("[genart] init=%s  %dx%d  effects=%d  buf0=%s buf1=%s\n",
                ok ? "true" : "false", lcd.width(), lcd.height(), NUM_EFFECTS,
                bufs[0] ? "OK" : "NULL", bufs[1] ? "OK" : "NULL");

  pinMode(PIN_BTN, INPUT_PULLUP);
  showLed(g_effect);

  // Both buffers start free.
  freeQ  = xQueueCreate(2, sizeof(int));
  readyQ = xQueueCreate(2, sizeof(int));
  for (int i = 0; i < 2; i++) xQueueSend(freeQ, &i, 0);

  // Producer on core 0; loop()/consumer stays on core 1.
  xTaskCreatePinnedToCore(renderTask, "render", 8192, nullptr, 1, nullptr, 0);

  Serial.println("[genart] running — press BOOT to cycle effects");
}

void loop() {
  static uint32_t t0 = 0, fps_n = 0;

  checkButton();

  int idx;
  if (xQueueReceive(readyQ, &idx, portMAX_DELAY) == pdTRUE) {
    uint32_t t = micros();
    sprites[idx]->pushSprite(0, 0);          // palette -> RGB565 + DMA
    g_pushUs = micros() - t;
    xQueueSend(freeQ, &idx, 0);
    fps_n++;
  }

  if (millis() - t0 > 1000) {
    Serial.printf("[genart] effect=%d (%s)  fps=%lu  render=%luus push=%luus\n",
                  g_effect, EFFECTS[g_effect].name, (unsigned long)fps_n,
                  (unsigned long)g_renderUs, (unsigned long)g_pushUs);
    fps_n = 0; t0 = millis();
  }
}
