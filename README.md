# ESP32-S3 Generative Art Display

Generative art on a **Waveshare ESP32-S3-LCD-1.47** — a 1.47" ST7789 IPS panel
(172×320, 262K colors) driven by an ESP32-S3 at 240 MHz. The goal is CPU-computed
framebuffer art (plasma, flow fields, noise fields, reaction-diffusion, etc.)
running at 30–60 fps, with the onboard **BOOT button** cycling between
effects/"shaders" and the **RGB LED** as an accent.

> Note: "shaders" here means per-pixel CPU rendering into a RAM framebuffer that
> is DMA'd to the display — the S3 has **no GPU**. See `CLAUDE.md` for the
> performance model.

## Hardware

| Item | Detail |
|---|---|
| Board | Waveshare ESP32-S3-LCD-1.47**B** (Type B) |
| MCU | ESP32-S3 (dual-core Xtensa LX7 @ 240 MHz), rev v0.2 |
| RAM | 512 KB internal SRAM + 8 MB PSRAM (N16R8) |
| Flash | 16 MB |
| Display | ST7789, 172×320 IPS, RGB565 |
| Controls | BOOT button (GPIO0), RESET |
| Extras | WS2812 RGB LED (GPIO38), microSD/TF slot |
| USB | Native USB-Serial/JTAG (VID 0x303A / PID 0x1001), enumerates as COM3 |

**Onboard QMI8658 IMU** (the 1.47B has a 6-axis IMU on I2C) — so "tilt control" is
doable with no extra hardware. Not yet wired up; exact I2C pins TBD from the schematic
(`docs/ESP32-S3-LCD-1.47B_schematic.pdf`).

### Confirmed pin map (ST7789, 4-wire SPI)

| Signal | GPIO |
|---|---|
| SCLK | 40 |
| MOSI | 45 |
| CS | 42 |
| DC | 41 |
| RST | 39 |
| Backlight (active HIGH) | **46** (1.47B; the base 1.47 uses 48 — see `CLAUDE.md`) |
| RGB LED (WS2812) | 38 |

microSD is on a dedicated SDMMC bus (CLK 14, CMD 15, D0 16, D1 18, D2 17, D3 21)
— not yet used.

## Toolchain

- **arduino-cli 1.5.0** (installed via winget to `C:\Program Files\Arduino CLI\`)
- **esp32:esp32 core 3.3.8**
- **LovyanGFX 1.2.21** (graphics library — fast ST7789 + DMA sprites)

The current PATH in a fresh shell may not include arduino-cli; prepend it:

```powershell
$env:Path = [System.Environment]::GetEnvironmentVariable('Path','Machine') + ';' + [System.Environment]::GetEnvironmentVariable('Path','User')
```

## Build & flash

```powershell
$FQBN = "esp32:esp32:esp32s3:PSRAM=disabled,FlashSize=16M,CDCOnBoot=cdc,FlashMode=qio"

arduino-cli compile --fqbn $FQBN .\display_test
arduino-cli upload  -p COM3 --fqbn $FQBN .\display_test
```

(`CDCOnBoot=cdc` makes `Serial` print over the native USB port. PSRAM is
currently disabled to keep first boots simple; enable `PSRAM=opi` later for
large asset buffers.)

## Sketches

| Sketch | Purpose | Status |
|---|---|---|
| `display_test/` | Color-bar + frame-counter validation pattern (sprite + DMA) | ✅ works (BL on GPIO46) |
| `bl_test/` | Blink backlight only (diagnostic) | superseded |
| `rgb_test/` | Cycle the WS2812 RGB LED on GPIO38 | ✅ works |

## Status

- ✅ Toolchain installed; board flashes reliably over COM3
- ✅ Chip + IO confirmed working (RGB LED cycles, serial heartbeat prints)
- ✅ **Display works** — the long black-screen bug was the wrong backlight pin:
  this is the **1.47B**, which puts the backlight on **GPIO46** (not 48). See
  `CLAUDE.md` → "RESOLVED" for the full story.
- ⏭️ Next: build the generative-art demo (plasma → flow field → …) with BOOT-button
  effect cycling. See `CLAUDE.md` → build plan.

See **`CLAUDE.md`** for the full working log and the build plan.
