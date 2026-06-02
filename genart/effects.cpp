// effects.cpp — sin/palette tables + effect implementations + the effect registry.
#include "effects.h"
#include <math.h>

// Shared sine lookup: sinLUT[i] = sin(i)*127 + 128, i.e. 0..255 centered on 128.
static uint8_t sinLUT[256];
uint8_t PALETTES[NUM_PALETTES][256][3];

static inline uint8_t clamp8(int v) { return v < 0 ? 0 : (v > 255 ? 255 : v); }

static void hsv2rgb(uint8_t h, uint8_t s, uint8_t v, uint8_t& r, uint8_t& g, uint8_t& b) {
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
    hsv2rgb((uint8_t)i, 255, 255, PALETTES[0][i][0], PALETTES[0][i][1], PALETTES[0][i][2]);
    // 1: fire (black -> red -> yellow -> white)
    PALETTES[1][i][0] = i < 85 ? i * 3 : 255;
    PALETTES[1][i][1] = i < 85 ? 0 : (i < 170 ? (i - 85) * 3 : 255);
    PALETTES[1][i][2] = i < 170 ? 0 : clamp8((i - 170) * 3);
    // 2: ocean (black -> blue -> cyan -> white)
    PALETTES[2][i][2] = i < 85 ? i * 3 : 255;
    PALETTES[2][i][1] = i < 85 ? 0 : (i < 170 ? (i - 85) * 3 : 255);
    PALETTES[2][i][0] = i < 170 ? 0 : clamp8((i - 170) * 3);
  }
}

// --- Effects --------------------------------------------------------------------
// Each fills buf[y*w + x] with a 0..255 palette index. Tilt (in.ax/ay) nudges the
// pattern origin so the art reacts to orientation once the IMU is live; with the
// default flat vector (0,0,1) these offsets are zero and behavior is unchanged.

static void fxPlasma(uint8_t* buf, int w, int h, const Inputs& in) {
  uint8_t t = (uint8_t)in.frame;
  int ox = (int)(in.ax * 48.0f);
  int oy = (int)(in.ay * 48.0f);
  for (int y = 0; y < h; y++) {
    int yo = y * w; int yy = y + oy;
    for (int x = 0; x < w; x++) {
      int xx = x + ox;
      int v = sinLUT[(uint8_t)(xx * 2 + t)]
            + sinLUT[(uint8_t)(yy * 2 + t)]
            + sinLUT[(uint8_t)((xx + yy) + (t << 1))]
            + sinLUT[(uint8_t)(((xx * xx + yy * yy) >> 5) + t)];
      buf[yo + x] = (uint8_t)(v >> 2);
    }
  }
}

static void fxRings(uint8_t* buf, int w, int h, const Inputs& in) {
  uint8_t t = (uint8_t)in.frame;
  int cx = w / 2 + (int)(in.ax * 60.0f);   // ring center drifts with tilt
  int cy = h / 2 + (int)(in.ay * 60.0f);
  for (int y = 0; y < h; y++) {
    int yo = y * w; int dy = y - cy;
    for (int x = 0; x < w; x++) {
      int dx = x - cx;
      uint32_t d2 = (uint32_t)(dx * dx + dy * dy);
      buf[yo + x] = sinLUT[(uint8_t)((d2 >> 6) - t)];
    }
  }
}

static void fxWeave(uint8_t* buf, int w, int h, const Inputs& in) {
  uint8_t t = (uint8_t)in.frame;
  int ox = (int)(in.ax * 32.0f);
  int oy = (int)(in.ay * 32.0f);
  for (int y = 0; y < h; y++) {
    int yo = y * w; int yy = y + oy;
    for (int x = 0; x < w; x++) {
      int xx = x + ox;
      int v = sinLUT[(uint8_t)(xx * 3 + t)]
            + sinLUT[(uint8_t)(yy * 3 - (t >> 1))]
            + sinLUT[(uint8_t)((xx - yy) * 2 + t)];
      buf[yo + x] = (uint8_t)((v * 85) >> 8);   // /3 -> 0..255
    }
  }
}

// --- Registry: the one place to add an effect -----------------------------------
const Effect EFFECTS[] = {
  { "plasma", fxPlasma, 0,  0, 30,  0 },
  { "rings",  fxRings,  1, 30,  0,  0 },
  { "weave",  fxWeave,  2,  0,  0, 30 },
};
const int NUM_EFFECTS = sizeof(EFFECTS) / sizeof(EFFECTS[0]);
