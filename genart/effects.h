// effects.h — the generative-art "standard".
//
// To add an effect: write a function with the EffectFn signature in effects.cpp,
// then append one row to the EFFECTS[] table (name, fn, palette id, LED color).
// Nothing else in the project needs to change.
//
// Effects write an 8-bit PALETTE INDEX (0..255) per pixel into `buf` (row-major,
// stride == w). The palette maps index->RGB565 at push time, so the same effect
// recolors for free by pointing at a different palette.
#pragma once
#include <stdint.h>

// Per-frame inputs handed to every effect. `frame` increments once per rendered
// frame. (ax,ay,az) is the accelerometer / tilt vector in ~g units, default
// (0,0,1) = lying flat / no tilt. The IMU fills these in later; effects can use
// them now and simply see "flat" until the IMU is wired up — no signature change.
struct Inputs {
  uint32_t frame;
  float ax, ay, az;
};

// Effects compute a 0..255 value per pixel and write `pal[value]` — a ready-made
// RGB565 color — straight into the 16bpp framebuffer. Writing RGB565 directly (vs
// 8-bit indices) makes the DMA push a pure blit with no per-pixel conversion, which
// is the difference between ~55 and ~80 fps. `pal` is the 256-entry palette for the
// active effect; effects stay palette-agnostic and recolor for free by being handed
// a different `pal`.
typedef void (*EffectFn)(uint16_t* buf, int w, int h, const Inputs& in, const uint16_t* pal);

struct Effect {
  const char* name;
  EffectFn    fn;
  uint8_t     palette;            // index into PALETTES / PAL565
  uint8_t     ledR, ledG, ledB;   // RGB LED color while this effect is active
};

extern const Effect EFFECTS[];
extern const int    NUM_EFFECTS;

// Palettes. PALETTES holds RGB888 (built by buildTables); PAL565 holds the same
// colors pre-converted to the panel's RGB565 (filled in setup() via lcd.color565
// so the byte order is guaranteed correct). Effects index PAL565.
// 0..2: cosine gradients used by the shaders. 3..10: curated 2-color gradients
// used by the sand (one picked per run). See effects.cpp.
static const int NUM_PALETTES = 11;
extern uint8_t  PALETTES[NUM_PALETTES][256][3];
extern uint16_t PAL565[NUM_PALETTES][256];

// Build the shared sin LUT and the RGB888 palettes. Call once in setup().
void buildTables();

// Seed the simulation PRNG (call once at startup, e.g. effectsSeed(esp_random())),
// so the randomized runs differ from boot to boot.
void effectsSeed(uint32_t s);
