# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

Working notes / resume context for the ESP32-S3 generative-art display project.
Read this first when returning.

> ✅ **DISPLAY WORKS (2026-06-01).** The multi-day black-screen bug was a wrong
> backlight pin: the board is the **1.47B**, which moves the backlight to **GPIO46**
> (the base 1.47 uses GPIO48). With BL on 46 the panel renders. See "RESOLVED" below.
>
> ✅ **GEN-ART APP BUILT (`genart/`, ~91 fps).** Dual-core 16bpp render pipeline at the
> SPI ceiling; effect framework + falling-sand sim with per-run variety; BOOT cycles
> effects. See "The app" section below. **Next: wire the onboard QMI8658 IMU for tilt.**

## Commands

arduino-cli is not on PATH in a fresh shell — prepend machine+user PATH first:

```powershell
$env:Path = [System.Environment]::GetEnvironmentVariable('Path','Machine') + ';' + [System.Environment]::GetEnvironmentVariable('Path','User')
$FQBN = "esp32:esp32:esp32s3:PSRAM=disabled,FlashSize=16M,CDCOnBoot=cdc,FlashMode=qio"

arduino-cli compile --fqbn $FQBN .\display_test          # build one sketch (folder name = .ino name)
arduino-cli upload  -p COM3 --fqbn $FQBN .\display_test  # flash over COM3
```

No tests/linters — this is firmware. "Running" = flash, then read the serial
heartbeat (see snippet below) and look at the panel. Each sketch is a separate
Arduino project: `foo/foo.ino` builds/uploads independently.

## Project goal

Generative art on the **Waveshare ESP32-S3-LCD-1.47B** (1.47" ST7789, 172×320).
(Confirmed by silkscreen + the official 1.47B schematic in `docs/`. This is the
"Type B" revision, NOT the base 1.47 — the difference matters: see backlight pin.)
CPU-computed framebuffer art at 30–60 fps; **BOOT button** cycles effects;
RGB LED accent. User asked for "best performance" → we chose **Arduino +
LovyanGFX** (near-C perf, DMA sprites). No GPU: "shaders" = per-pixel CPU render
into a RAM framebuffer, then DMA to the panel.

Performance model: 172×320×2B ≈ 110 KB/frame fits in fast internal SRAM (keep
the framebuffer there, NOT PSRAM — PSRAM is slower for per-pixel writes; use it
for assets). SPI push at 80 MHz ≈ ~11 ms (~90 fps ceiling); real fps is bounded
by pixel-math cost. Use double-buffered LGFX_Sprite + DMA, compute on one core
while the other transfers.

"Tilt control" the user wanted **IS possible** — the **1.47B has an onboard
QMI8658 6-axis IMU on I2C** (confirmed in the 1.47B schematic, `docs/`). The old
note here ("no onboard IMU") was based on the base-1.47 and is WRONG for this board.
Tilt-driven effects need no extra hardware. (Exact IMU_SDA/SCL GPIOs still TBD —
read them off the schematic before wiring it up.) Start with the BOOT button for
effect-switching; add tilt later.

## Hardware (all confirmed)

- ESP32-S3 rev v0.2, dual-core LX7 240 MHz, **8 MB PSRAM, 16 MB flash** (N16R8).
- Native USB-Serial/JTAG: **VID 0x303A / PID 0x1001**, shows up as **COM3**.
  (COM1 is an unrelated built-in port — ignore it.)
- ST7789 172×320 IPS, RGB565.
- **Confirmed pins (1.47B, from the official 1.47B schematic in `docs/`):**
  SCLK 40, MOSI 45, CS 42, DC 41, RST 39, **BL 46 (active HIGH)**, RGB LED (WS2812) 38.
  4-wire SPI (dedicated DC pin). microSD SDMMC: CLK 14, CMD 15, D0 16, D1 18, D2 17, D3 21.
  - ⚠️ **BL is GPIO46 on the 1.47B, NOT 48.** Every base-1.47 source (3D-Box repo,
    ESPP header, Waveshare's own 1.47 demo, the `waveshare_esp32_s3_lcd_147` core
    variant) says BL=48 — that's the *base board* and was the multi-day red herring.
    Backlight defaults OFF (10K gate pulldown), so drive GPIO46 HIGH explicitly.
  - The 1.47B also has an onboard **QMI8658 IMU** (I2C) for tilt; pins TBD from schematic.
- The official esp32 core variant `waveshare_esp32_s3_lcd_147` confirms RGB_LED=38
  (shared with the 1.47B). Do NOT trust that variant for BL — it's 48 (base board).

## Toolchain / environment

- Windows 11, PowerShell. Working dir `C:\code\esp`.
- **arduino-cli 1.5.0** at `C:\Program Files\Arduino CLI\arduino-cli.exe`
  (installed via `winget install ArduinoSA.CLI`). **Gotcha:** not on PATH in a
  fresh shell — prepend machine+user PATH first (see README).
- **esp32:esp32 3.3.8** core; **LovyanGFX 1.2.21** lib (both installed).
- FQBN: `esp32:esp32:esp32s3:PSRAM=disabled,FlashSize=16M,CDCOnBoot=cdc,FlashMode=qio`
- Flashing works perfectly every time (esptool connects, writes, verifies, resets).

### Read serial (no blocking monitor) — handy snippet
```powershell
$port = new-Object System.IO.Ports.SerialPort COM3,115200,None,8,one
$port.ReadTimeout = 1500; $port.Open(); Start-Sleep -Milliseconds 300
$deadline = (Get-Date).AddSeconds(5)
while ((Get-Date) -lt $deadline) { try { $port.ReadLine() } catch {} }
$port.Close()
```

## What WORKS

- ✅ Flash/upload over COM3.
- ✅ **RGB LED** (`rgb_test/`) cycles colors via `rgbLedWrite(38, r,g,b)` — so IO
  control + the pin family are correct. (LED is GRB-ordered; user saw green/red/blue
  for my red/green/blue calls — cosmetic only.)
- ✅ Sketch runs without crashing: `display_test` reaches its loop and prints a
  serial heartbeat (`alive, frame=N`). So `lcd.init()` completes.

## RESOLVED: display was black → fixed by BL on GPIO46 (2026-06-01)

**Root cause: wrong backlight pin for this board revision.** The board is the
**1.47B**, whose backlight MOSFET gate is on **GPIO46**; every source we'd trusted
(3D-Box repo, ESPP header, Waveshare's 1.47 demo, the esp32 core variant) gives the
**base-1.47** value GPIO48. With BL on the wrong pin the backlight never turned on,
so an otherwise-correct render showed nothing. Fix: drive **GPIO46 HIGH** (active
HIGH; defaults off via a 10K gate pulldown). Confirmed working — color-bar pattern
renders. Authoritative pinout: `docs/ESP32-S3-LCD-1.47B_schematic.pdf`.

**Why it took so long (lessons for next time):**
- `lcd.init()` returns **true even when nothing is wired right** — `readable=false`
  means LovyanGFX can't read the panel back, so it can't detect a bad bus/pinout.
  Init success + sprite-alloc success told us nothing about the panel. Don't trust it.
- The config being **byte-identical to a "known-working repo" was a trap** — that repo
  is for the *base 1.47*, and the panel/SPI pins happen to be identical, so everything
  looked right except the one pin that moved (BL). Secondary sources (search summaries,
  even spotpear) kept conflating 1.47 / 1.47B / Touch and parroting BL=48.
- What finally cracked it: two user observations — backlight **completely dark** (not
  lit-but-black) + silkscreen reads **1.47B** — then reading the **1.47B schematic PDF**
  directly (rendered to images; its text layer OCRs wrong). Lesson: for Waveshare boards,
  go to the schematic for the exact revision; treat repo configs as hypotheses.

**Other real fixes made along the way (keep these):**
- Backlight is driven by plain `digitalWrite`, NOT LovyanGFX `Light_PWM`. Its LEDC path
  is broken on esp32 core 3.x (we're on 3.3.8; LovyanGFX issue #708). To restore PWM
  dimming later, use the core-3.x `ledcAttach(46,freq,bits)` / `ledcWrite(46,duty)` API.
- `display_test` renders into a full-screen `LGFX_Sprite` and `pushSprite`s it (DMA),
  the path the gen-art demo will use.
- `spi_3wire=false` (this panel is 4-wire with a real DC pin — schematic-confirmed).

### Useful references
- **Authoritative 1.47B pinout:** `docs/ESP32-S3-LCD-1.47B_schematic.pdf` (BL=46!).
- LovyanGFX config base (this *family*, ~80 fps 3D cube, but base-1.47 BL=48):
  `github.com/ahmadrezarazian/Waveshare_ESP32-S3-LCD1.47_3D-Box` (`src/main.cpp`).
- LovyanGFX backlight/LEDC-on-core-3.x bug: LovyanGFX issue #708.
- Waveshare wiki + product pages 403 on WebFetch; use the schematic PDF or search.

## The app: `genart/` — architecture & learnings (built, ~91 fps)

Modular structure (the "standard" — adding an effect is one function + one
`EFFECTS[]` row):
- `board.h` — pins + verified `LGFX` panel class + `SCREEN_W/H`. Single source of
  hardware truth; no globals (declare `LGFX lcd;` in the .ino).
- `effects.h` / `effects.cpp` — `Inputs` struct (frame + `ax/ay/az` tilt) handed to
  every effect; `EFFECTS[]` registry; sin LUT + palettes. Effects: `sand` (default),
  `plasma`, `rings`, `weave`.
- `genart.ino` — dual-core pipeline, BOOT cycling, RGB LED, fps/timing prints.

### Performance learnings (40 → 55 → 91 fps; SPI-bound ceiling)
- **Dual-core double buffer:** core 0 (pinned FreeRTOS task) renders the next frame
  while core 1 (`loop`) DMA-pushes the ready one; buffers ping-pong via two FreeRTOS
  queues (`freeQ`/`readyQ`). Frame time = `max(render, push)`, not the sum. (40→55.)
- **Write RGB565 directly, not 8bpp+palette:** with 16bpp sprites the effect writes
  `pal[idx]` (a ready 565 value) so `pushSprite` is a pure DMA blit — no per-pixel
  palette conversion on the push core. (55→91.) Push is then ~11 ms = the raw SPI
  transfer time for a full frame at 80 MHz = the **hard ceiling**. render ~2–10 ms.
- **16bpp sprite byte order:** a 16bpp `LGFX_Sprite` stores pixels BYTE-SWAPPED
  (MSB first). Raw buffer writes must swap too, or colors rotate (pure blue `0x001F`
  → `0x1F00` shows as green). Build the 565 palette with `lcd.color565()` **then
  byte-swap**. (The 8bpp path didn't need this — the library swapped for us.)
- `lcd.init()` returns **true even with a wrong pinout** (`readable=false` → it can't
  read the panel back). Init success proves nothing; don't trust it for diagnosis.

### Effect/sim learnings
- **Paletted look:** effects compute a 0..255 value; a per-effect/per-run palette LUT
  maps it to color, so recolor is free. Curated 2-color *cyclic* gradients (triangle
  ramp A→B→A) look classier than full-HSV rainbow.
- **Falling sand** (`fxSand`): 86×160 grid (2px cells), bottom-up CA. Stateful via
  file-static grid — safe because only the render task (core 0) touches it.
  - Run physics **every frame** with each grain acting at ~50% probability → smooth
    91 Hz motion but lazy average speed and natural scatter. Gating physics to every
    2nd frame instead produced a visible ~45 Hz strobe on falling grains.
  - Detect "settled" grains (occupied with support below) to (a) reset at ~95% full
    without the falling stream tripping it, and to reason about pile height.
  - Per-run randomization (palette, spray width/count, wind strength/freqs, emitter
    motion pattern) keyed off a `sandNewRun()`; seed PRNG from `esp_random()` at boot.
  - Smooth motion = sines / low-passed noise. A raw per-frame random walk reads jagged.

### Roadmap
- **IMU tilt (next):** onboard QMI8658 on I2C (pins ~IO43/IO44 — confirm SDA/SCL order
  + addr 0x6A/0x6B from schematic; use `Wire.begin(43,44)`, lib `SensorLib 0.4.1`).
  Feed gravity into `Inputs.ax/ay` (already plumbed) so tilt pours the sand.
- More sims (water/walls, Game of Life), optional PWM brightness via core-3.x
  `ledcAttach`, microSD presets.

## Conventions
- Each sketch in its own folder (Arduino requires `foo/foo.ino`). `genart/` is the app
  (multiple files compiled together); the others are single-file diagnostics.
- `board.h` is the shared, verified hardware config — reuse it; don't re-derive pins.
