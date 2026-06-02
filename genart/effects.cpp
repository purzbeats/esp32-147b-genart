// effects.cpp — sin/palette tables + effect implementations + the effect registry.
#include "effects.h"
#include <math.h>
#include <string.h>

// Small fast PRNG (xorshift32) for the simulations.
static uint32_t s_rng = 0x1234567u;
static inline uint32_t xr() { s_rng ^= s_rng << 13; s_rng ^= s_rng >> 17; s_rng ^= s_rng << 5; return s_rng; }

// Shared sine lookup: sinLUT[i] = sin(i)*127 + 128, i.e. 0..255 centered on 128.
static uint8_t sinLUT[256];
uint8_t  PALETTES[NUM_PALETTES][256][3];   // RGB888 source palettes
uint16_t PAL565[NUM_PALETTES][256];        // RGB565, filled in setup() (see genart.ino)

static inline uint8_t clamp8(int v) { return v < 0 ? 0 : (v > 255 ? 255 : v); }

// Inigo-Quilez cosine gradient: color(t) = a + b*cos(2pi*(c*t + d)), per channel.
// These produce smooth, harmonious, band-free gradients — the look behind most
// good procedural color. https://iquilezles.org/articles/palettes/
static void cosPalette(int idx, const float a[3], const float b[3],
                       const float c[3], const float d[3]) {
  for (int i = 0; i < 256; i++) {
    float t = i / 256.0f;
    for (int k = 0; k < 3; k++) {
      float v = a[k] + b[k] * cosf(6.28318531f * (c[k] * t + d[k]));
      PALETTES[idx][i][k] = clamp8((int)(v * 255.0f + 0.5f));
    }
  }
}

void buildTables() {
  for (int i = 0; i < 256; i++)
    sinLUT[i] = (uint8_t)(sinf(i * 2.0f * (float)M_PI / 256.0f) * 127.0f + 128.0f);

  const float a[3] = { 0.5f, 0.5f, 0.5f };
  const float b[3] = { 0.5f, 0.5f, 0.5f };
  // 0 — "spectrum": smooth full-hue flow (cosine, so soft, not garish)
  { const float c[3] = {1,1,1};      const float d[3] = {0.00f, 0.33f, 0.67f}; cosPalette(0,a,b,c,d); }
  // 1 — "ember": warm amber -> magenta -> violet
  { const float c[3] = {1,1,1};      const float d[3] = {0.30f, 0.20f, 0.20f}; cosPalette(1,a,b,c,d); }
  // 2 — "lagoon": teal -> blue -> indigo
  { const float c[3] = {1,1,0.5f};   const float d[3] = {0.55f, 0.60f, 0.70f}; cosPalette(2,a,b,c,d); }
}

// --- Effects --------------------------------------------------------------------
// Each fills buf[y*w + x] with pal[value] (a ready RGB565 color). Time is run slow
// (frame>>1) so motion is languid at 90 fps, and a slow palette scroll (+shift)
// lets the colors breathe. Tilt (in.ax/ay) nudges the origin; default flat = no-op.

static void fxPlasma(uint16_t* buf, int w, int h, const Inputs& in, const uint16_t* pal) {
  uint8_t t     = (uint8_t)(in.frame >> 1);
  uint8_t shift = (uint8_t)(in.frame >> 2);     // gentle color drift
  int ox = (int)(in.ax * 48.0f);
  int oy = (int)(in.ay * 48.0f);
  for (int y = 0; y < h; y++) {
    int yo = y * w; int yy = y + oy;
    for (int x = 0; x < w; x++) {
      int xx = x + ox;
      int v = sinLUT[(uint8_t)(xx + t)]                 // low spatial frequency:
            + sinLUT[(uint8_t)(yy + (t >> 1))]          // big soft blobs that flow
            + sinLUT[(uint8_t)(((xx + yy) >> 1) + t)]
            + sinLUT[(uint8_t)(((xx - yy) >> 1) - t)];
      buf[yo + x] = pal[(uint8_t)((v >> 2) + shift)];
    }
  }
}

static void fxRings(uint16_t* buf, int w, int h, const Inputs& in, const uint16_t* pal) {
  uint8_t t     = (uint8_t)(in.frame >> 1);
  uint8_t shift = (uint8_t)(in.frame >> 3);
  int cx = w / 2 + (int)(in.ax * 60.0f);        // ripple center drifts with tilt
  int cy = h / 2 + (int)(in.ay * 60.0f);
  for (int y = 0; y < h; y++) {
    int yo = y * w; int dy = y - cy;
    for (int x = 0; x < w; x++) {
      int dx = x - cx;
      uint32_t d2 = (uint32_t)(dx * dx + dy * dy);
      uint8_t v = sinLUT[(uint8_t)((d2 >> 7) - t)];     // >>7: fewer, larger rings
      buf[yo + x] = pal[(uint8_t)(v + shift)];
    }
  }
}

static void fxWeave(uint16_t* buf, int w, int h, const Inputs& in, const uint16_t* pal) {
  uint8_t t     = (uint8_t)(in.frame >> 1);
  uint8_t shift = (uint8_t)(in.frame >> 2);
  int ox = (int)(in.ax * 32.0f);
  int oy = (int)(in.ay * 32.0f);
  for (int y = 0; y < h; y++) {
    int yo = y * w; int yy = y + oy;
    for (int x = 0; x < w; x++) {
      int xx = x + ox;
      uint8_t a = sinLUT[(uint8_t)(xx + t)];
      uint8_t b = sinLUT[(uint8_t)(yy + (t >> 1))];
      uint8_t c = sinLUT[(uint8_t)(((xx + yy) >> 1) - t)];
      int v = ((a * b) >> 8) + c;                       // product -> soft moiré blobs
      buf[yo + x] = pal[(uint8_t)((v >> 1) + shift)];
    }
  }
}

// --- Falling sand (cellular automaton, pixel-art) -------------------------------
// Persistent grid of grains rendered as CELL x CELL blocks. Self-emits colored
// grains at the top; they fall and slide diagonally to pile up. Physics steps at
// ~half the frame rate for a calm fall. Tilt (in.ax) biases the slide direction —
// once the IMU lands this becomes full tilt-to-pour gravity.
#define SAND_CELL 2
#define SAND_GW   86          // SCREEN_W / SAND_CELL (172/2)
#define SAND_GH   160         // SCREEN_H / SAND_CELL (320/2)
static uint8_t sgrid[SAND_GH * SAND_GW];   // 0 = empty, else grain color index 1..255
static bool    sandInit = false;

static void fxSand(uint16_t* buf, int w, int h, const Inputs& in, const uint16_t* pal) {
  if (!sandInit) { memset(sgrid, 0, sizeof(sgrid)); sandInit = true; }

  if ((in.frame & 1) == 0) {                 // step physics ~every 2nd frame
    // Emit a little stream near the top center; color drifts so piles are rainbow.
    uint8_t col = (uint8_t)(1 + ((in.frame >> 1) % 255));
    int ex = SAND_GW / 2;
    for (int k = 0; k < 3; k++) {
      int xx = ex + (int)(xr() % 7) - 3;
      if (xx >= 0 && xx < SAND_GW && sgrid[xx] == 0) sgrid[xx] = col;
    }

    int bias = in.ax > 0.15f ? 1 : (in.ax < -0.15f ? -1 : 0);   // tilt -> slide bias
    for (int y = SAND_GH - 2; y >= 0; y--) {                    // bottom-up
      bool ltr = ((y + (in.frame >> 1)) & 1);                   // alternate scan dir
      for (int i = 0; i < SAND_GW; i++) {
        int x = ltr ? i : (SAND_GW - 1 - i);
        int here = y * SAND_GW + x;
        uint8_t v = sgrid[here];
        if (!v) continue;
        int down = here + SAND_GW;
        if (sgrid[down] == 0) { sgrid[down] = v; sgrid[here] = 0; continue; }
        bool dl = (x > 0)           && sgrid[down - 1] == 0;
        bool dr = (x < SAND_GW - 1) && sgrid[down + 1] == 0;
        if (dl && dr) {
          bool right = bias > 0 ? true : (bias < 0 ? false : (xr() & 1));
          sgrid[down + (right ? 1 : -1)] = v; sgrid[here] = 0;
        } else if (dl) { sgrid[down - 1] = v; sgrid[here] = 0; }
        else if (dr)   { sgrid[down + 1] = v; sgrid[here] = 0; }
      }
    }
    // When the pile reaches the emitter, avalanche-reset for a fresh fill.
    if (sgrid[SAND_GW / 2] != 0 && sgrid[SAND_GW / 2 + 1] != 0) memset(sgrid, 0, sizeof(sgrid));
  }

  // Render grid -> framebuffer as solid CELL x CELL blocks (empty = black).
  for (int y = 0; y < SAND_GH; y++) {
    for (int x = 0; x < SAND_GW; x++) {
      uint8_t v = sgrid[y * SAND_GW + x];
      uint16_t c = v ? pal[v] : 0x0000;
      int px = x * SAND_CELL, py = y * SAND_CELL;
      for (int by = 0; by < SAND_CELL; by++) {
        uint16_t* row = &buf[(py + by) * w + px];
        for (int bx = 0; bx < SAND_CELL; bx++) row[bx] = c;
      }
    }
  }
}

// --- Registry: the one place to add an effect -----------------------------------
const Effect EFFECTS[] = {
  { "sand",   fxSand,   0, 28, 18,  0 },   // pixel-art falling-sand simulation
  { "plasma", fxPlasma, 0,  0, 20, 12 },
  { "rings",  fxRings,  1, 24,  6,  0 },
  { "weave",  fxWeave,  2,  0,  6, 24 },
};
const int NUM_EFFECTS = sizeof(EFFECTS) / sizeof(EFFECTS[0]);
