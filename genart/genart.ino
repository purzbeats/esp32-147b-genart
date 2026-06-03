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
#include "video.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>

LGFX lcd;
LGFX_Sprite spr0(&lcd), spr1(&lcd);
LGFX_Sprite* sprites[2] = { &spr0, &spr1 };
uint16_t*    bufs[2]    = { nullptr, nullptr };

// A "scene" is either a procedural effect (index 0..NUM_EFFECTS-1) or an SD clip
// (NUM_EFFECTS..NUM_EFFECTS+g_nClips-1). BOOT steps through all of them with one
// counter, so it cycles effects *and* clips.
volatile int   g_scene  = 0;
int            g_nClips = 0;
int            sceneCount()      { return NUM_EFFECTS + g_nClips; }
bool           sceneIsVideo(int s){ return s >= NUM_EFFECTS; }

volatile float g_ax = 0.0f, g_ay = 0.0f, g_az = 1.0f;   // tilt; flat until IMU lands
volatile uint32_t g_renderUs = 0, g_pushUs = 0;          // timing diagnostics

QueueHandle_t freeQ, readyQ;   // carry buffer indices (0/1) between the two cores

void showLed(int s) {
  if (sceneIsVideo(s)) rgbLedWrite(PIN_RGB, 0, 24, 24);  // cyan for video scenes
  else                 rgbLedWrite(PIN_RGB, EFFECTS[s].ledR, EFFECTS[s].ledG, EFFECTS[s].ledB);
}
const char* sceneName(int s) {
  return sceneIsVideo(s) ? videoName(s - NUM_EFFECTS) : EFFECTS[s].name;
}

// Producer (core 0): take a free buffer, render into it, hand it to the consumer.
// Effects are pure pixel-math; video scenes decode an MJPEG frame off the SD card
// (paced to the clip's fps inside videoRenderFrame). Both write byte-swapped RGB565.
void renderTask(void*) {
  uint32_t frame = 0;
  for (;;) {
    int idx;
    xQueueReceive(freeQ, &idx, portMAX_DELAY);
    int s = g_scene;
    uint32_t t = micros();
    if (sceneIsVideo(s)) {
      videoRenderFrame(bufs[idx], SCREEN_W, SCREEN_H, s - NUM_EFFECTS);
    } else {
      Inputs in = { frame, g_ax, g_ay, g_az };
      EFFECTS[s].fn(bufs[idx], SCREEN_W, SCREEN_H, in, PAL565[EFFECTS[s].palette]);
    }
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
      // Scenes are read at render time, so switching is just this — no per-sprite
      // palette to update. Wraps through effects then SD clips.
      g_scene = (g_scene + 1) % sceneCount();
      showLed(g_scene);
      Serial.printf("[genart] scene -> %d (%s)\n", g_scene, sceneName(g_scene));
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
  effectsSeed(esp_random());     // hardware RNG so randomized runs differ each boot
  // Convert the RGB888 palettes to RGB565. A 16bpp LGFX_Sprite stores pixels
  // BYTE-SWAPPED (MSB first, to match SPI), so when we write raw values to the
  // buffer we must swap too — otherwise colors rotate (pure blue 0x001F -> 0x1F00
  // reads as green). The old 8bpp path didn't need this; the library swapped for us.
  for (int p = 0; p < NUM_PALETTES; p++)
    for (int i = 0; i < 256; i++) {
      uint16_t c = lcd.color565(PALETTES[p][i][0], PALETTES[p][i][1], PALETTES[p][i][2]);
      PAL565[p][i] = (uint16_t)((c >> 8) | (c << 8));   // byte-swap
    }

  pinMode(PIN_BTN, INPUT_PULLUP);

  // ALLOCATION ORDER MATTERS. The render-task stack needs a 16 KB *contiguous* block;
  // the two 110 KB framebuffers (and the SD driver's buffers) fragment internal SRAM,
  // so if we allocate them first the largest free hole shrinks below 16 KB and task
  // creation fails (errCOULD_NOT_ALLOCATE_REQUIRED_MEMORY) — the producer never runs
  // and the pipeline stalls (black screen). So: create the queues + render task FIRST,
  // while the heap is still fresh and contiguous. The task immediately blocks on the
  // empty freeQ and waits harmlessly until we seed it after the framebuffers exist.
  freeQ  = xQueueCreate(2, sizeof(int));
  readyQ = xQueueCreate(2, sizeof(int));

  // Producer on core 0; loop()/consumer stays on core 1. Larger stack: the video
  // scene runs SD reads + JPEG decode here.
  TaskHandle_t rt = nullptr;
  BaseType_t rc = xTaskCreatePinnedToCore(renderTask, "render", 16384, nullptr, 1, &rt, 0);
  Serial.printf("[genart] renderTask create rc=%d handle=%p\n", (int)rc, rt);

  // Both 110 KB framebuffers live in PSRAM. Internal SRAM can't hold two of them
  // *plus* the 16 KB render stack and the video libs' buffers (SD_MMC/FS/JPEGDEC) —
  // the 2nd allocation fails and the render task then writes through a NULL pointer
  // (StoreProhibited crash). PSRAM has 8 MB to spare. Cost: per-pixel effect writes
  // to PSRAM are slower than SRAM, so procedural effects run below the old 91 fps —
  // an acceptable trade for video mode coexisting (video is ~20 fps regardless).
  for (int i = 0; i < 2; i++) {
    sprites[i]->setPsram(true);                    // allocate the framebuffer in PSRAM
    sprites[i]->setColorDepth(16);                 // RGB565: push is a pure DMA blit
    bufs[i] = (uint16_t*)sprites[i]->createSprite(SCREEN_W, SCREEN_H);
  }
  Serial.printf("[genart] init=%s  %dx%d  effects=%d  buf0=%s buf1=%s  freeHeap=%u\n",
                ok ? "true" : "false", lcd.width(), lcd.height(), NUM_EFFECTS,
                bufs[0] ? "OK" : "NULL", bufs[1] ? "OK" : "NULL",
                (unsigned)ESP.getFreeHeap());

  g_nClips = videoInit();        // mount SD + scan for *.avi; clips become extra scenes
  Serial.printf("[genart] scenes=%d (%d effects + %d clips)\n",
                sceneCount(), NUM_EFFECTS, g_nClips);
  showLed(g_scene);

  // Buffers are built — release them to the producer, which has been waiting on freeQ.
  for (int i = 0; i < 2; i++) xQueueSend(freeQ, &i, 0);

  Serial.println("[genart] running — press BOOT to cycle scenes (effects + clips)");
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
    if (sceneIsVideo(g_scene))
      // For video, render includes the pacing delay — decode= is the real frame cost.
      Serial.printf("[genart] scene=%d (%s)  fps=%lu  decode=%luus push=%luus\n",
                    g_scene, sceneName(g_scene), (unsigned long)fps_n,
                    (unsigned long)videoLastDecodeUs(), (unsigned long)g_pushUs);
    else
      Serial.printf("[genart] scene=%d (%s)  fps=%lu  render=%luus push=%luus\n",
                    g_scene, sceneName(g_scene), (unsigned long)fps_n,
                    (unsigned long)g_renderUs, (unsigned long)g_pushUs);
    fps_n = 0; t0 = millis();
  }
}
