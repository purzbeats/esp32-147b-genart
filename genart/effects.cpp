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

// Fill one CELLxCELL block of the framebuffer with a solid color. Shared by every
// grid-based simulation (sand + the CA sims) so the index->pixel blit lives in one
// place. (gx,gy) is the grid cell; cell is its pixel size.
static inline void cellBlock(uint16_t* buf, int w, int gx, int gy, int cell, uint16_t c) {
  int px = gx * cell, py = gy * cell;
  for (int by = 0; by < cell; by++) {
    uint16_t* row = &buf[(py + by) * w + px];
    for (int bx = 0; bx < cell; bx++) row[bx] = c;
  }
}

#define PAL_FIRE 11   // index of the bespoke forest-fire heat palette (built below)

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

  // 11 — forest-fire heat ramp (NOT cyclic). Three zones the fire sim indexes by
  // cell state: 0 = empty (black); 1..127 = living tree (dark->mid green); 128..255
  // = burning ember cooling through dark-red -> orange -> yellow -> white-hot.
  auto lerp = [](int a, int b, int t, int span) { return a + (b - a) * t / span; };
  for (int i = 0; i < 256; i++) {
    int r, g, b;
    if (i == 0) { r = g = b = 0; }                                   // empty
    else if (i < 128) {                                              // tree (green)
      r = lerp(8, 70, i - 1, 126); g = lerp(24, 150, i - 1, 126); b = lerp(6, 40, i - 1, 126);
    } else if (i < 180) {                                            // dark red -> orange
      r = lerp(60, 220, i - 128, 52); g = lerp(6, 80, i - 128, 52);  b = 0;
    } else if (i < 220) {                                            // orange -> yellow
      r = lerp(220, 255, i - 180, 40); g = lerp(80, 190, i - 180, 40); b = lerp(0, 40, i - 180, 40);
    } else {                                                         // yellow -> white-hot
      r = 255; g = lerp(190, 250, i - 220, 35); b = lerp(40, 210, i - 220, 35);
    }
    PALETTES[PAL_FIRE][i][0] = clamp8(r);
    PALETTES[PAL_FIRE][i][1] = clamp8(g);
    PALETTES[PAL_FIRE][i][2] = clamp8(b);
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

  // Reset when the pile fills up to ~95% height. Check the single row at the 5%
  // line and require it broadly filled (> half the width). This sits BELOW the
  // emitter's cluster at the very top (which otherwise causes spurious early
  // resets, especially on wide/dense-spray runs), and the narrow falling stream
  // can't fill half a row — only the risen pile does.
  int line = SAND_GH / 20;                                   // row at 5% from top
  int fill = 0;
  for (int x = 0; x < SAND_GW; x++) if (sgrid[line * SAND_GW + x]) fill++;
  if (fill > SAND_GW / 2) {
    memset(sgrid, 0, sizeof(sgrid));
    sandNewRun();                                            // new run: palette + pattern + spray + wind
  }

  // Render grid -> framebuffer as solid CELL x CELL blocks (empty = black).
  for (int y = 0; y < SAND_GH; y++)
    for (int x = 0; x < SAND_GW; x++) {
      uint8_t v = sgrid[y * SAND_GW + x];
      cellBlock(buf, w, x, y, SAND_CELL, v ? spal[v] : 0x0000);
    }
}

// --- Shared CA scratch ----------------------------------------------------------
// Single next-generation buffer reused by every double-buffered CA below (Life,
// CCA, fire). Safe to share: exactly one effect renders at a time (the render task
// on core 0), and each step fully overwrites the prefix it uses before copying back.
// Sized to the largest CA grid (the 86x160 2px grids); Life uses only the prefix.
static uint8_t s_scratch[SAND_GW * SAND_GH];

// --- Conway's Game of Life (age-colored) ----------------------------------------
// Toroidal CA on a 43x80 grid (4px cells -> bold, readable patterns). Cells carry
// an *age* (frames survived, saturating at 255) which indexes the run's cyclic
// palette: newborns sit at the lo color, mature cells bloom to the hi color, then
// elders cycle back — so gliders and oscillators leave shimmering trails. Reseeds a
// fresh random soup (new palette + density) on extinction, stasis, or a max age.
#define LF_CELL 4
#define LF_GW   43            // SCREEN_W / LF_CELL (172/4)
#define LF_GH   80            // SCREEN_H / LF_CELL (320/4)
static uint8_t  lgrid[LF_GW * LF_GH];
static bool     lifeInit = false;
static int      lifePal  = SAND_PAL_FIRST;
static uint32_t lifePrevPop = 0, lifeStable = 0, lifeGen = 0;

static void lifeSeed() {
  lifePal = SAND_PAL_FIRST + (int)(xr() % SAND_PAL_COUNT);
  int dens = 25 + (int)(xr() % 25);                    // 25..49% live
  for (int i = 0; i < LF_GW * LF_GH; i++)
    lgrid[i] = ((int)(xr() % 100) < dens) ? 1 : 0;
  lifePrevPop = 0; lifeStable = 0; lifeGen = 0;
}

static void fxLife(uint16_t* buf, int w, int h, const Inputs& in, const uint16_t* pal) {
  if (!lifeInit) { lifeSeed(); lifeInit = true; }
  const uint16_t* lpal = PAL565[lifePal];

  if (in.frame % 5 == 0) {                              // step generation ~18 Hz
    uint32_t pop = 0;
    for (int y = 0; y < LF_GH; y++) {
      int ym = ((y - 1 + LF_GH) % LF_GH) * LF_GW, yc = y * LF_GW, yp = ((y + 1) % LF_GH) * LF_GW;
      for (int x = 0; x < LF_GW; x++) {
        int xm = (x - 1 + LF_GW) % LF_GW, xp = (x + 1) % LF_GW;
        int n = (lgrid[ym + xm] > 0) + (lgrid[ym + x] > 0) + (lgrid[ym + xp] > 0)
              + (lgrid[yc + xm] > 0)                       + (lgrid[yc + xp] > 0)
              + (lgrid[yp + xm] > 0) + (lgrid[yp + x] > 0) + (lgrid[yp + xp] > 0);
        uint8_t v = lgrid[yc + x], nv;
        if (v) nv = (n == 2 || n == 3) ? (v < 255 ? v + 1 : 255) : 0;   // survive (age++)
        else   nv = (n == 3) ? 1 : 0;                                   // birth
        s_scratch[yc + x] = nv;
        if (nv) pop++;
      }
    }
    memcpy(lgrid, s_scratch, LF_GW * LF_GH);
    lifeStable = (pop == lifePrevPop) ? lifeStable + 1 : 0;   // count stalled generations
    lifePrevPop = pop;
    if (pop == 0 || lifeStable > 24 || ++lifeGen > 360) lifeSeed();
  }

  for (int y = 0; y < LF_GH; y++)
    for (int x = 0; x < LF_GW; x++) {
      uint8_t v = lgrid[y * LF_GW + x];
      cellBlock(buf, w, x, y, LF_CELL, v ? lpal[v] : 0x0000);
    }
}

// --- Cyclic cellular automaton (spiral waves) -----------------------------------
// Griffeath cyclic CA on the 86x160 grid: each cell has a phase 0..N-1 and advances
// to (phase+1) mod N when >= threshold von-Neumann neighbors already hold that next
// phase. From random noise this self-organizes into rotating spiral waves (a
// Belousov-Zhabotinsky look). Phase maps straight onto a cyclic palette, so the
// spirals read as rotating color bands. New N / threshold / palette each run.
#define CC_CELL 2
#define CC_GW   SAND_GW       // 86
#define CC_GH   SAND_GH       // 160
static uint8_t  ccgrid[CC_GW * CC_GH];
static bool     ccInit = false;
static int      ccPal = SAND_PAL_FIRST, ccN = 12, ccThresh = 1;
static uint32_t ccAge = 0;

static void ccaSeed() {
  ccPal    = SAND_PAL_FIRST + (int)(xr() % SAND_PAL_COUNT);
  ccN      = 6 + (int)(xr() % 11);                     // 6..16 phases
  ccThresh = 1 + (int)(xr() % 2);                      // 1 or 2 (2 = chunkier waves)
  for (int i = 0; i < CC_GW * CC_GH; i++) ccgrid[i] = (uint8_t)(xr() % ccN);
  ccAge = 0;
}

static void fxCCA(uint16_t* buf, int w, int h, const Inputs& in, const uint16_t* pal) {
  if (!ccInit) { ccaSeed(); ccInit = true; }
  const uint16_t* cpal = PAL565[ccPal];

  if (in.frame % 2 == 0) {                              // step ~45 Hz
    for (int y = 0; y < CC_GH; y++) {
      int ym = ((y - 1 + CC_GH) % CC_GH) * CC_GW, yc = y * CC_GW, yp = ((y + 1) % CC_GH) * CC_GW;
      for (int x = 0; x < CC_GW; x++) {
        int xm = (x - 1 + CC_GW) % CC_GW, xp = (x + 1) % CC_GW;
        uint8_t p = ccgrid[yc + x], nextp = (p + 1) % ccN;
        int cnt = (ccgrid[ym + x] == nextp) + (ccgrid[yp + x] == nextp)
                + (ccgrid[yc + xm] == nextp) + (ccgrid[yc + xp] == nextp);
        s_scratch[yc + x] = (cnt >= ccThresh) ? nextp : p;
      }
    }
    memcpy(ccgrid, s_scratch, CC_GW * CC_GH);
    if (++ccAge > 1400) ccaSeed();
  }

  for (int y = 0; y < CC_GH; y++)
    for (int x = 0; x < CC_GW; x++) {
      uint8_t idx = (uint8_t)(ccgrid[y * CC_GW + x] * 255 / (ccN - 1));
      cellBlock(buf, w, x, y, CC_CELL, cpal[idx]);
    }
}

// --- Forest fire (Drossel-Schwabl) ----------------------------------------------
// Self-organizing 3-state CA on the 86x160 grid. Cell states packed into one byte:
// 0 = empty, 1 = living tree, 128..255 = burning ember (the value is its heat, which
// cools each step until it goes out). A tree ignites if a neighbor burns or rarely
// from "lightning"; empty cells regrow trees at a small probability. Fronts of fire
// sweep through the forest and green regrows behind — endless, no reset needed
// (params re-roll occasionally for variety). Uses the bespoke PAL_FIRE heat ramp.
#define FR_CELL 2
#define FR_GW   SAND_GW       // 86
#define FR_GH   SAND_GH       // 160
static uint8_t  fgrid[FR_GW * FR_GH];
static bool     fireInit = false;
static uint32_t fireGrow = 1200, fireLight = 12, fireAge = 0;   // probabilities out of 65536

static void fireSeed() {
  fireGrow  = 600 + (xr() % 1600);                     // tree regrowth prob
  fireLight = 3 + (xr() % 18);                         // lightning-strike prob
  for (int i = 0; i < FR_GW * FR_GH; i++) fgrid[i] = (xr() % 100 < 40) ? 1 : 0;
  fireAge = 0;
}

static void fxFire(uint16_t* buf, int w, int h, const Inputs& in, const uint16_t* pal) {
  if (!fireInit) { fireSeed(); fireInit = true; }
  const uint16_t* fpal = PAL565[PAL_FIRE];

  if (in.frame % 2 == 0) {                              // step ~45 Hz
    for (int y = 0; y < FR_GH; y++) {
      int ym = ((y - 1 + FR_GH) % FR_GH) * FR_GW, yc = y * FR_GW, yp = ((y + 1) % FR_GH) * FR_GW;
      for (int x = 0; x < FR_GW; x++) {
        int xm = (x - 1 + FR_GW) % FR_GW, xp = (x + 1) % FR_GW;
        uint8_t v = fgrid[yc + x], nv;
        if (v >= 128) {                                // burning -> cool, then burn out
          nv = (v >= 128 + 24) ? v - 24 : 0;
        } else if (v == 1) {                           // tree -> maybe ignite
          bool nb = fgrid[ym + x] >= 128 || fgrid[yp + x] >= 128 || fgrid[yc + xm] >= 128 || fgrid[yc + xp] >= 128
                 || fgrid[ym + xm] >= 128 || fgrid[ym + xp] >= 128 || fgrid[yp + xm] >= 128 || fgrid[yp + xp] >= 128;
          nv = (nb || (xr() & 0xFFFF) < fireLight) ? 255 : 1;
        } else {                                       // empty -> maybe regrow
          nv = ((xr() & 0xFFFF) < fireGrow) ? 1 : 0;
        }
        s_scratch[yc + x] = nv;
      }
    }
    memcpy(fgrid, s_scratch, FR_GW * FR_GH);
    if (++fireAge > 3000) fireSeed();
  }

  for (int y = 0; y < FR_GH; y++)
    for (int x = 0; x < FR_GW; x++) {
      uint8_t v = fgrid[y * FR_GW + x];
      uint16_t c = (v == 0) ? fpal[0] : (v == 1) ? fpal[90] : fpal[v];  // empty/tree/ember
      cellBlock(buf, w, x, y, FR_CELL, c);
    }
}

// --- Reaction-diffusion (Gray-Scott) --------------------------------------------
// The showpiece: two chemicals U and V diffuse and react, producing organic Turing
// patterns (spots, stripes, coral, mitosis). Run in float (the S3 has an FPU) on a
// 43x80 grid (4px cells) with several iterations per displayed frame so the pattern
// visibly grows. The (f,k) feed/kill pair picks the morphology; a small table of
// known-interesting pairs is rolled per run alongside a cyclic palette mapping V.
#define RD_CELL 4
#define RD_GW   43            // SCREEN_W / RD_CELL
#define RD_GH   80            // SCREEN_H / RD_CELL
static float    rU[RD_GW * RD_GH], rV[RD_GW * RD_GH], rU2[RD_GW * RD_GH], rV2[RD_GW * RD_GH];
static bool     rdInit = false;
static int      rdPal = SAND_PAL_FIRST;
static float    rdF = 0.0545f, rdK = 0.062f;
static uint32_t rdAge = 0;

// Hand-picked (feed, kill) pairs, each a different pattern regime.
static const float RD_FK[][2] = {
  { 0.0367f, 0.0649f },   // mitosis (dividing cells)
  { 0.0545f, 0.0620f },   // worms / spots
  { 0.0260f, 0.0510f },   // moving waves
  { 0.0140f, 0.0450f },   // solitons
  { 0.0390f, 0.0580f },   // maze
  { 0.0300f, 0.0560f },   // coral
};

static void rdSeed() {
  rdPal = SAND_PAL_FIRST + (int)(xr() % SAND_PAL_COUNT);
  int p = (int)(xr() % (sizeof(RD_FK) / sizeof(RD_FK[0])));
  rdF = RD_FK[p][0]; rdK = RD_FK[p][1];
  for (int i = 0; i < RD_GW * RD_GH; i++) { rU[i] = 1.0f; rV[i] = 0.0f; }
  for (int s = 0; s < 25; s++) {                       // sprinkle V seed blobs
    int cx = xr() % RD_GW, cy = xr() % RD_GH;
    for (int dy = -2; dy <= 2; dy++)
      for (int dx = -2; dx <= 2; dx++) {
        int x = (cx + dx + RD_GW) % RD_GW, y = (cy + dy + RD_GH) % RD_GH;
        rU[y * RD_GW + x] = 0.25f; rV[y * RD_GW + x] = 0.5f;
      }
  }
  rdAge = 0;
}

static void fxReaction(uint16_t* buf, int w, int h, const Inputs& in, const uint16_t* pal) {
  if (!rdInit) { rdSeed(); rdInit = true; }
  const uint16_t* rpal = PAL565[rdPal];
  const float Du = 0.16f, Dv = 0.08f;

  for (int it = 0; it < 10; it++) {                    // many micro-steps per frame
    for (int y = 0; y < RD_GH; y++) {
      int ym = ((y - 1 + RD_GH) % RD_GH) * RD_GW, yc = y * RD_GW, yp = ((y + 1) % RD_GH) * RD_GW;
      for (int x = 0; x < RD_GW; x++) {
        int xm = (x - 1 + RD_GW) % RD_GW, xp = (x + 1) % RD_GW;
        float u = rU[yc + x], v = rV[yc + x];
        float lapU = (rU[yc + xm] + rU[yc + xp] + rU[ym + x] + rU[yp + x]) * 0.2f
                   + (rU[ym + xm] + rU[ym + xp] + rU[yp + xm] + rU[yp + xp]) * 0.05f - u;
        float lapV = (rV[yc + xm] + rV[yc + xp] + rV[ym + x] + rV[yp + x]) * 0.2f
                   + (rV[ym + xm] + rV[ym + xp] + rV[yp + xm] + rV[yp + xp]) * 0.05f - v;
        float uvv = u * v * v;
        float nu = u + (Du * lapU - uvv + rdF * (1.0f - u));
        float nv = v + (Dv * lapV + uvv - (rdF + rdK) * v);
        rU2[yc + x] = nu < 0 ? 0 : (nu > 1 ? 1 : nu);
        rV2[yc + x] = nv < 0 ? 0 : (nv > 1 ? 1 : nv);
      }
    }
    memcpy(rU, rU2, sizeof(rU));
    memcpy(rV, rV2, sizeof(rV));
  }
  if (++rdAge > 2600) rdSeed();

  for (int y = 0; y < RD_GH; y++)
    for (int x = 0; x < RD_GW; x++) {
      int idx = (int)(rV[y * RD_GW + x] * 640.0f);
      cellBlock(buf, w, x, y, RD_CELL, rpal[idx > 255 ? 255 : idx]);
    }
}

// --- Registry: the one place to add an effect -----------------------------------
const Effect EFFECTS[] = {
  { "sand",     fxSand,     0, 28, 18,  0 },   // pixel-art falling-sand simulation
  { "plasma",   fxPlasma,   0,  0, 20, 12 },
  { "rings",    fxRings,    1, 24,  6,  0 },
  { "weave",    fxWeave,    2,  0,  6, 24 },
  { "life",     fxLife,     0,  4, 22,  8 },   // Conway's Life, age-colored (own palette)
  { "cca",      fxCCA,      2, 16,  4, 22 },   // cyclic CA spiral waves (own palette)
  { "fire",     fxFire,     0, 28,  6,  0 },   // forest-fire CA (PAL_FIRE heat ramp)
  { "reaction", fxReaction, 1,  0, 18, 16 },   // Gray-Scott reaction-diffusion (own palette)
};
const int NUM_EFFECTS = sizeof(EFFECTS) / sizeof(EFFECTS[0]);
