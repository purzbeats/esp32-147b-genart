// effects.cpp — sin/palette tables + effect implementations + the effect registry.
#include "effects.h"
#include <math.h>
#include <string.h>

// Small fast PRNG (xorshift32) for the simulations.
static uint32_t s_rng = 0x1234567u;
static inline uint32_t xr() { s_rng ^= s_rng << 13; s_rng ^= s_rng >> 17; s_rng ^= s_rng << 5; return s_rng; }
void effectsSeed(uint32_t s) { if (s) s_rng = s; }

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

// Curated 2-color gradients for the sand (lo -> hi). Each stays in a tight, classy
// hue range instead of sweeping the spectrum; built as cyclic A->B->A ramps so the
// color bands flow without a seam. One is chosen per sand "run".
static const int SAND_PAL_FIRST = 3;
static const int SAND_PAL_COUNT = 8;
static const uint8_t SAND_PAIRS[SAND_PAL_COUNT][6] = {
  {  10, 10, 40,    80, 220, 230 },  // midnight -> aqua
  {  60,  8, 20,   240, 200,  90 },  // wine -> gold
  {  30, 10, 60,   240, 140, 200 },  // indigo -> rose
  {   5, 40, 40,   160, 240, 200 },  // deep teal -> mint
  {  20, 20, 30,   240, 160,  40 },  // charcoal -> amber
  {  40,  5, 60,   250, 120,  90 },  // violet -> coral
  {  10, 40, 15,   180, 230,  90 },  // forest -> lime
  {  40, 40, 90,   250, 200, 160 },  // slate -> peach
};

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

  // 3..10 — curated 2-color sand gradients (cyclic A->B->A via a triangle ramp).
  for (int p = 0; p < SAND_PAL_COUNT; p++)
    for (int i = 0; i < 256; i++) {
      int tri = i < 128 ? i * 2 : (255 - i) * 2;       // 0..254..0
      for (int k = 0; k < 3; k++) {
        int lo = SAND_PAIRS[p][k], hi = SAND_PAIRS[p][k + 3];
        PALETTES[SAND_PAL_FIRST + p][i][k] = (uint8_t)(lo + (hi - lo) * tri / 255);
      }
    }
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
static int     sandPal  = SAND_PAL_FIRST;  // this run's gradient (re-picked each reset)

// Per-run emitter motion: a smooth combination of one or two sines (fluid, coherent
// for the whole run). ~18% of runs add a gentle, heavily-smoothed noise wobble for a
// "slightly erratic" feel that's still fluid — never the old frame-to-frame jitter.
static float eF1 = 0.006f, eA1 = 0.34f, eF2 = 0.0f, eA2 = 0.0f, eNoiseAmt = 0.0f;
static float emaNoise = 0.0f;
static int   eSprayW = 3, eSprayN = 3;                         // spray half-width / grains per burst
static float eWindAmp = 1.0f, eWindF1 = 0.012f, eWindF2 = 0.037f;  // breeze strength / gust freqs

static void sandNewRun() {
  sandPal = SAND_PAL_FIRST + (int)(xr() % SAND_PAL_COUNT);     // bespoke palette per run
  if ((xr() % 100) < 18) {                                     // slightly erratic, still fluid
    eF1 = 0.0090f; eA1 = 0.20f; eF2 = 0.0230f; eA2 = 0.10f; eNoiseAmt = 0.45f;
  } else {
    switch (xr() % 4) {                                        // calm, uniform sweeps
      case 0:  eF1 = 0.0060f; eA1 = 0.34f; eF2 = 0.0f;    eA2 = 0.0f;  eNoiseAmt = 0.0f; break; // wide & slow
      case 1:  eF1 = 0.0090f; eA1 = 0.26f; eF2 = 0.0130f; eA2 = 0.12f; eNoiseAmt = 0.0f; break; // gentle double
      case 2:  eF1 = 0.0040f; eA1 = 0.18f; eF2 = 0.0f;    eA2 = 0.0f;  eNoiseAmt = 0.0f; break; // lazy & narrow
      default: eF1 = 0.0120f; eA1 = 0.30f; eF2 = 0.0f;    eA2 = 0.0f;  eNoiseAmt = 0.0f; break; // steady swing
    }
  }
  eSprayW  = 1 + (int)(xr() % 8);                              // spray half-width 1..8
  eSprayN  = 2 + (int)(xr() % 4);                              // grains per burst 2..5
  eWindAmp = (xr() % 121) / 100.0f;                            // breeze 0..1.2 (some runs near-calm)
  eWindF1  = 0.008f + (xr() % 100) * 0.0001f;                  // gust freqs vary per run
  eWindF2  = 0.025f + (xr() % 100) * 0.0002f;
}

static void fxSand(uint16_t* buf, int w, int h, const Inputs& in, const uint16_t* pal) {
  if (!sandInit) {
    memset(sgrid, 0, sizeof(sgrid));
    sandNewRun();
    sandInit = true;
  }
  const uint16_t* spal = PAL565[sandPal];    // this run's curated colors (ignores passed pal)

  // Emit (gated to ~half rate so the emission density is independent of the 91 Hz
  // sim). Grain color drifts through the run's gradient over time.
  if ((in.frame & 1) == 0) {
    uint8_t col = (uint8_t)(1 + ((in.frame >> 1) % 255));
    // Emitter follows this run's smooth motion pattern (set in sandNewRun()).
    float sweep = sinf(in.frame * eF1) * eA1 + sinf(in.frame * eF2) * eA2;
    if (eNoiseAmt > 0.0f) {                                   // rare runs: gentle fluid wobble
      float rnd = ((int)(xr() % 2001) - 1000) / 1000.0f;      // -1..1
      emaNoise = emaNoise * 0.985f + rnd * 0.015f;            // heavy low-pass -> smooth, not jagged
      sweep += emaNoise * eNoiseAmt * 3.0f;
    }
    int ex = SAND_GW / 2 + (int)(sweep * (SAND_GW * 0.5f));
    for (int k = 0; k < eSprayN; k++) {
      int xx = ex + (int)(xr() % (2 * eSprayW + 1)) - eSprayW;
      if (xx >= 0 && xx < SAND_GW && sgrid[xx] == 0) sgrid[xx] = col;
    }
  }

  // Physics runs EVERY frame (91 Hz) for smooth motion, but each grain acts with
  // ~50% probability per frame. That halves the average fall speed (keeping the
  // lazy feel) while spreading the timing so there's no fixed-rate strobe — the
  // stream scatters naturally instead. (~45 Hz/grain rate keeps breeze identical.)
  float wind = (sinf(in.frame * eWindF1) * 0.6f + sinf(in.frame * eWindF2) * 0.4f) * eWindAmp;
  int      wdir  = wind > 0.15f ? 1 : (wind < -0.15f ? -1 : 0);
  uint32_t wprob = (uint32_t)(fabsf(wind) * 60.0f);          // 0..~60 of 256
  int tilt = in.ax > 0.15f ? 1 : (in.ax < -0.15f ? -1 : 0);
  int bias = tilt != 0 ? tilt : wdir;                        // tilt wins; else wind
  for (int y = SAND_GH - 2; y >= 0; y--) {                   // bottom-up
    bool ltr = ((y + in.frame) & 1);                         // alternate scan dir
    for (int i = 0; i < SAND_GW; i++) {
      int x = ltr ? i : (SAND_GW - 1 - i);
      int here = y * SAND_GW + x;
      uint8_t v = sgrid[here];
      if (!v) continue;
      if (xr() & 1) continue;                                // act ~half the frames -> lazy avg speed
      int down = here + SAND_GW;
      if (sgrid[down] == 0) { sgrid[down] = v; sgrid[here] = 0; continue; }
      bool dl = (x > 0)           && sgrid[down - 1] == 0;
      bool dr = (x < SAND_GW - 1) && sgrid[down + 1] == 0;
      if (dl && dr) {
        bool right = bias > 0 ? true : (bias < 0 ? false : (xr() & 1));
        sgrid[down + (right ? 1 : -1)] = v; sgrid[here] = 0;
      } else if (dl) { sgrid[down - 1] = v; sgrid[here] = 0; }
      else if (dr)   { sgrid[down + 1] = v; sgrid[here] = 0; }
      else if (wdir != 0 && (xr() & 0xFF) < wprob) {         // breeze blows a resting grain
        int nx = x + wdir;
        if (nx >= 0 && nx < SAND_GW && sgrid[y * SAND_GW + nx] == 0) {
          sgrid[y * SAND_GW + nx] = v; sgrid[here] = 0;
        }
      }
    }
  }

  // Reset once the settled pile reaches the top ~5% of the screen (95% full).
  // Only count "settled" grains (something solid below) so the falling stream
  // passing through the top rows doesn't trip the reset early.
  int topBand = SAND_GH / 20;                                // top 5% of rows
  int settledTop = 0;
  for (int y = 0; y < topBand; y++)
    for (int x = 0; x < SAND_GW; x++) {
      int idx = y * SAND_GW + x;
      if (sgrid[idx] && sgrid[idx + SAND_GW]) settledTop++;   // occupied with support below
    }
  if (settledTop > SAND_GW / 8) {
    memset(sgrid, 0, sizeof(sgrid));
    sandNewRun();                                            // new run: palette + pattern + spray + wind
  }

  // Render grid -> framebuffer as solid CELL x CELL blocks (empty = black).
  for (int y = 0; y < SAND_GH; y++) {
    for (int x = 0; x < SAND_GW; x++) {
      uint8_t v = sgrid[y * SAND_GW + x];
      uint16_t c = v ? spal[v] : 0x0000;
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
