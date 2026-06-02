# ESP32-S3 Generative Art Display

Real-time generative art on a **Waveshare ESP32-S3-LCD-1.47B** — a 1.47" ST7789 IPS
panel (172×320, RGB565) driven by a dual-core ESP32-S3 at 240 MHz. Everything is
**CPU-rendered into a RAM framebuffer and DMA'd to the panel** (the S3 has no GPU),
running at the panel's **~91 fps SPI ceiling**. The **BOOT button** cycles effects and
the onboard **RGB LED** reflects the active one.

The headline piece is a **falling-sand cellular automaton** with a gusty breeze,
hand-picked color palettes, and a different "personality" every run.

## The effects

Cycle with the BOOT button:

| # | Effect | What it is |
|---|--------|-----------|
| 0 | **sand** | Falling-sand simulation (pixel-art, 2px cells). A wandering emitter sprays colored grains that fall, slide, and pile into shifting dunes; a gusty breeze sculpts the surface; the pile avalanche-resets near full. Each run randomizes palette, spray width, wind, and emitter motion. |
| 1 | **plasma** | Classic sin-LUT plasma — big soft blobs flowing through a smooth color gradient. |
| 2 | **rings** | Concentric ripples expanding from a center. |
| 3 | **weave** | Soft interference/moiré from multiplied sine fields. |

All four render into a full-screen sprite at ~91 fps.

### Falling sand details

- **Pixel-art grid** of 86×160 cells (2px each) — ~13.7k grains.
- **91 Hz physics** with each grain acting at ~50% probability per frame: smooth
  motion at the full frame rate, but a lazy average fall speed with natural scatter
  (no fixed-rate strobe).
- **Gusty breeze** — a two-sine wind nudges resting grains sideways into dunes.
- **Curated palettes** — 8 tasteful 2-color cyclic gradients (midnight→aqua,
  wine→gold, violet→coral, …), one rolled per run, instead of a garish full rainbow.
- **Per-run variety** — spray width, grains-per-burst, wind strength/rhythm, and
  emitter motion pattern are all randomized each run; PRNG seeded from `esp_random()`
  so the sequence differs each boot.
- **Smart reset** — avalanche-resets when the *settled* pile (grains with support
  below, ignoring the falling stream) reaches the top 5% of the screen.

## Architecture

The app (`genart/`) is built around a small, reusable structure:

- **`board.h`** — the single source of truth for the hardware: pins, the verified
  LovyanGFX `LGFX` panel class, and screen dimensions.
- **`effects.h` / `effects.cpp`** — the effect *standard*. An `Inputs` struct (frame
  counter + `ax/ay/az` tilt) is handed to every effect; effects are registered in one
  `EFFECTS[]` table. **Adding an effect is one function + one table row.** The tilt
  fields are already plumbed for the (planned) IMU.
- **`genart.ino`** — orchestration only: the dual-core render pipeline, BOOT-button
  cycling, RGB LED, and fps/timing instrumentation.

### Rendering pipeline (how it hits 91 fps)

Frames are **double-buffered across both cores**:

- **Core 0** (a pinned FreeRTOS task) computes the next frame into a free buffer.
- **Core 1** (the Arduino `loop`) DMA-pushes the ready buffer to the panel.
- Buffers ping-pong through two FreeRTOS queues, so compute and transfer overlap and
  the frame time is `max(render, push)` instead of their sum.

Effects write **RGB565 directly** (via a per-effect palette LUT) into a 16bpp sprite,
so `pushSprite` is a pure DMA blit with no per-pixel conversion. The push then costs
~11 ms — the raw SPI transfer time for a full frame at 80 MHz — which is the ceiling.

The journey on a single effect: **40 fps** (naïve single-buffer) → **55 fps**
(dual-core overlap) → **91 fps** (16bpp direct-write, conversion removed).

## Hardware

| Item | Detail |
|---|---|
| Board | Waveshare ESP32-S3-LCD-1.47**B** (Type B) |
| MCU | ESP32-S3 (dual-core Xtensa LX7 @ 240 MHz), rev v0.2 |
| RAM | 512 KB internal SRAM + 8 MB PSRAM (N16R8) |
| Flash | 16 MB |
| Display | ST7789, 172×320 IPS, RGB565, 4-wire SPI @ 80 MHz |
| Controls | BOOT button (GPIO0), RESET |
| Extras | WS2812 RGB LED (GPIO38), QMI8658 IMU (I2C), microSD/TF slot |
| USB | Native USB-Serial/JTAG, enumerates as COM3 |

### ⚠️ The backlight gotcha (cost us days)

This is the **1.47B**, whose LCD **backlight is on GPIO46** — *not* GPIO48 like the
base 1.47. Every third-party config and even the official esp32 core variant lists 48,
so the panel initialized fine but stayed dark. If you fork this for the base 1.47,
change `PIN_BL` back to 48. Authoritative pinout: `docs/ESP32-S3-LCD-1.47B_schematic.pdf`.

### Confirmed pin map

| Signal | GPIO | | Signal | GPIO |
|---|---|---|---|---|
| SCLK | 40 | | RST | 39 |
| MOSI | 45 | | **Backlight** (active HIGH) | **46** |
| CS | 42 | | RGB LED (WS2812) | 38 |
| DC | 41 | | IMU I2C (SDA/SCL) | 43 / 44 *(to confirm)* |

microSD is on a dedicated SDMMC bus (CLK 14, CMD 15, D0 16, D1 18, D2 17, D3 21) — unused.

## Toolchain

- **arduino-cli 1.5.0**, **esp32:esp32 core 3.3.8**, **LovyanGFX 1.2.21**

> **Note:** the backlight is driven with a plain `digitalWrite(46, HIGH)`, *not*
> LovyanGFX's `Light_PWM` — its LEDC path is broken on esp32 core 3.x ([LovyanGFX
> #708](https://github.com/lovyan03/LovyanGFX/issues/708)) and silently leaves the
> backlight off. PWM dimming can be restored later via the core-3.x `ledcAttach` API.

## Build & flash

arduino-cli may not be on PATH in a fresh shell; prepend it first:

```powershell
$env:Path = [System.Environment]::GetEnvironmentVariable('Path','Machine') + ';' + [System.Environment]::GetEnvironmentVariable('Path','User')
$FQBN = "esp32:esp32:esp32s3:PSRAM=disabled,FlashSize=16M,CDCOnBoot=cdc,FlashMode=qio"

arduino-cli compile --fqbn $FQBN .\genart
arduino-cli upload  -p COM3 --fqbn $FQBN .\genart
```

`CDCOnBoot=cdc` routes `Serial` over the native USB port (fps/timing prints there).

## Sketches

| Sketch | Purpose | Status |
|---|---|---|
| `genart/` | **The app** — dual-core gen-art engine: sand sim + 3 shaders, BOOT cycling | ✅ ~91 fps |
| `display_test/` | Color-bar + frame-counter panel validation (sprite + DMA) | ✅ works |
| `rgb_test/` | Cycle the WS2812 RGB LED on GPIO38 | ✅ works |
| `bl_test/` | Blink the backlight only (diagnostic) | superseded |

## Status & roadmap

- ✅ Panel working (after the GPIO46 backlight discovery), reliable flash over COM3
- ✅ Dual-core 16bpp render pipeline at the ~91 fps SPI ceiling
- ✅ Effect framework + falling-sand simulation with per-run variety
- ⏭️ **Next:** wire the onboard **QMI8658 IMU** so tilting the board becomes gravity
  (pour the sand around, wind fighting your tilt — the `Inputs.ax/ay/az` hook is ready)
- ⏭️ More sims (water/walls in the sand, Game of Life), optional PWM brightness, microSD presets

See **`CLAUDE.md`** for the full engineering log, the debugging story, and learnings.
