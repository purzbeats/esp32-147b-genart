# CLAUDE.md — working notes for this project

Resume context for the ESP32-S3 generative-art display project. Read this first
when returning. (User stepped away mid-debug on 2026-06-01.)

## Project goal

Generative art on the **Waveshare ESP32-S3-LCD-1.47** (1.47" ST7789, 172×320).
CPU-computed framebuffer art at 30–60 fps; **BOOT button** cycles effects;
RGB LED accent. User asked for "best performance" → we chose **Arduino +
LovyanGFX** (near-C perf, DMA sprites). No GPU: "shaders" = per-pixel CPU render
into a RAM framebuffer, then DMA to the panel.

Performance model: 172×320×2B ≈ 110 KB/frame fits in fast internal SRAM (keep
the framebuffer there, NOT PSRAM — PSRAM is slower for per-pixel writes; use it
for assets). SPI push at 80 MHz ≈ ~11 ms (~90 fps ceiling); real fps is bounded
by pixel-math cost. Use double-buffered LGFX_Sprite + DMA, compute on one core
while the other transfers.

"Tilt control" the user wanted is **NOT possible out of the box** — no onboard
IMU. Needs an external I2C IMU (QMI8658/MPU6050) on the header. Defer; use the
BOOT button for effect-switching for now.

## Hardware (all confirmed)

- ESP32-S3 rev v0.2, dual-core LX7 240 MHz, **8 MB PSRAM, 16 MB flash** (N16R8).
- Native USB-Serial/JTAG: **VID 0x303A / PID 0x1001**, shows up as **COM3**.
  (COM1 is an unrelated built-in port — ignore it.)
- ST7789 172×320 IPS, RGB565.
- **Confirmed pins:** SCLK 40, MOSI 45, CS 42, DC 41, RST 39, **BL 48 (active HIGH)**,
  RGB LED (WS2812) 38. microSD SDMMC: CLK 14, CMD 15, D0 16, D1 18, D2 17, D3 21.
- The official esp32 core variant `waveshare_esp32_s3_lcd_147` confirms RGB_LED=38.

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

## OPEN PROBLEM: display is still black

The panel shows nothing across **four** attempts, even after copying a config
from a confirmed-working project for this exact board.

### What we've RULED OUT
- Pins — confirmed by 3 independent sources + Waveshare's own demo (BL=48 HIGH).
- Crash/hang — serial proves the sketch runs and loops fine.
- Backlight-only blink test was **a red herring**: an uninitialized ST7789 has its
  display OFF and blocks light, so backlight alone can't show a glow. Inconclusive,
  not a failure.
- `spi_3wire` — fixed from `true`→`false` (this panel is 4-wire with a real DC pin).
  This was a real bug but did NOT fix the black screen.
- Build flags — the working reference repo
  (`ahmadrezarazian/Waveshare_ESP32-S3-LCD1.47_3D-Box`) builds bone-stock
  `esp32-s3-devkitm-1` with no flags. Our build is not the difference.
- Our `display_test` LGFX class now **matches that working repo's config exactly**
  (SPI3_HOST, 80 MHz, spi_3wire=false, memory 320×172, panel 172×320, offset_x=34,
  invert=true, rgb_order=false, BL PWM on 48).

### Last change made before user left
Rewrote `display_test` to **redraw every frame** (was a one-shot draw in setup) with
a live `f=` frame counter + a sweeping white line, then reflashed. Rationale: rule
out a first-frame-lost-during-reset race. **Need user's eyes to confirm result.**

### PRIORITIZED next steps (need the user watching the screen)
1. **Confirm current state:** does the screen now show climbing color bars with an
   incrementing `f=` counter? If yes → solved (it was the one-shot race); move to
   gen-art demo. If still black → continue.
2. **Lower SPI clock:** try `freq_write = 40000000`, then `26000000`. 80 MHz may be
   marginal on this unit/cable even though it works on the reference board.
3. **Longer/explicit reset:** some panels need a longer RST low pulse or a post-init
   `lcd.invertDisplay(true/false)` toggle. Try toggling invert at runtime to see any
   change.
4. **Cross-check with a different driver** to isolate LovyanGFX: install
   `Arduino_GFX` (`arduino-cli lib install "GFX Library for Arduino"`) and use its
   documented ST7789 databus config for this board. If it ALSO stays black →
   strongly implies hardware.
5. **Hardware/variant check:** confirm the user's board is the base `1.47` and not
   the `1.47B` (Type B) revision, which may have a different panel/pinout. Ask the
   user to read the silkscreen / re-check the listing. Also: protective film on?
   Try a different USB-C **data** cable; reseat anything.
6. **Compare against the working repo's exact init** by reading more of its
   `src/main.cpp` (it draws into an `LGFX_Sprite` and pushes it — try that exact
   path: create a full-screen sprite, fill it, `sprite.pushSprite(0,0)`).

### Useful references
- Working LovyanGFX project (this exact board, ~80 fps 3D cube):
  `github.com/ahmadrezarazian/Waveshare_ESP32-S3-LCD1.47_3D-Box` (`src/main.cpp`).
- TFT_eSPI discussion (confirms pins; needed HSPI port): Bodmer/TFT_eSPI #3527.
- ESPP example page: esp-cpp.github.io/espp/ws_s3_lcd_1_47_example.html
- Waveshare wiki: waveshare.com/wiki/ESP32-S3-LCD-1.47 (WebFetch 403s; use search).

## Once the display works — build plan (task #5)

1. Full-screen `LGFX_Sprite` framebuffer in internal SRAM (RGB565).
2. Effects as functions writing into the sprite (start: plasma via sin LUTs, then
   Perlin/flow field, fire, reaction-diffusion).
3. `sprite.pushSprite()` with DMA; measure fps; aim 30–60.
4. BOOT button (GPIO0, `INPUT_PULLUP`, active LOW) debounced to cycle effects;
   RGB LED reflects current effect.
5. Later: microSD for presets/palettes; optional external IMU for tilt.

## Conventions
- Each sketch in its own folder (Arduino requires `foo/foo.ino`).
- Keep the confirmed LGFX class identical across sketches (factor into a shared
  header once the display is proven).
