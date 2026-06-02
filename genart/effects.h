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

typedef void (*EffectFn)(uint8_t* buf, int w, int h, const Inputs& in);

struct Effect {
  const char* name;
  EffectFn    fn;
  uint8_t     palette;            // index into PALETTES
  uint8_t     ledR, ledG, ledB;   // RGB LED color while this effect is active
};

extern const Effect EFFECTS[];
extern const int    NUM_EFFECTS;

// Palettes: [palette][index] -> {r,g,b}. Built by buildTables().
static const int NUM_PALETTES = 3;
extern uint8_t PALETTES[NUM_PALETTES][256][3];

// Build the shared sin LUT and all palettes. Call once in setup().
void buildTables();
