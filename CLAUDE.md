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
- **Board outline: 36.37 × 20.32 mm** (≈1.43″ × 0.80″), USB-C overhangs one end.
  Headers are 2.54 mm pitch; left edge exposes `BAT`/`G`, right edge `3V3`/`G`/`VBUS`.

### Battery / power (confirmed from schematic + board photo, 2026-06-02)
- **Decision (2026-06-02): powered over the USB-C cable for now** — no cell soldered.
  Simplest, no polarity/auto-shutoff risk. The onboard charging hardware below is
  documented for if we ever want it cordless later.
- **Onboard LiPo charging is fully present** — no add-on board needed, just a cell.
  - **`ETA6098`** single-cell Li-ion/LiPo charger with power-path: charges from USB
    `VBUS`, auto-switches the system to battery when USB is unplugged.
  - **`CHG`** silkscreen + LED near the USB-C = charge-status indicator.
  - **`BAT_ADC`** net: battery voltage is wired to an ADC pin for a fuel gauge (TODO:
    confirm which GPIO from the schematic; pairs naturally with the IMU work).
- **No battery connector is populated** — the cell connects via the **`BAT` and `G`
  through-hole pads** on the left header (`BAT` = battery +, adjacent `G` = −). So a
  battery is *soldered on* (or solder a JST pigtail to `BAT`/`G` for a removable cell).
- ⚠️ **Polarity is unprotected** — bare pads, no keyed plug. Reverse voltage into `BAT`
  feeds the charger/power-path and can kill the board. **Meter the cell leads first;**
  don't trust wire colors on cheap LiPos.
- **Cell:** 1S 3.7 V LiPo. Footprint-matched size = **502035** (5×20×35 mm). Current
  build uses a 502035 **330 mAh** → ~2–2.5 h runtime (board draws ~120–160 mA active,
  WiFi/BT off). Charges in <1 h; if the cell warms during charge, the ETA6098's fixed
  charge current is just a bit brisk for a 330 mAh cell (harmless).

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

**Reading order for a new instance:** `board.h` (hardware truth) → `effects.h`
(the effect contract: `Inputs`, `EffectFn`, `Effect`/`EFFECTS[]`) → `effects.cpp`
(effects + the sand sim) → `genart.ino` (the pipeline that ties it together).

### Performance learnings (40 → 55 → 91 fps; SPI-bound ceiling)
- **Dual-core double buffer:** core 0 (pinned FreeRTOS task) renders the next frame
  while core 1 (`loop`) DMA-pushes the ready one; buffers ping-pong via two FreeRTOS
  queues (`freeQ`/`readyQ`). Frame time = `max(render, push)`, not the sum. (40→55.)
  - **Invariant:** exactly **2** buffers, both seeded into `freeQ` at startup, and
    both queues are **depth 2**. Render pulls from `freeQ`→pushes to `readyQ`; loop
    does the reverse. Don't change the buffer count or queue depth independently —
    they must match or the pipeline stalls/overruns.
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
- **Video mode (✅ CONFIRMED PLAYING on hardware 2026-06-02):** plays MJPEG/AVI clips off
  the microSD card. Verified end-to-end with multiple clips cycling as scenes, correct
  colors, no crashes. **Raw decode (SD read + JPEG) measured ~22–26 ms/frame** → max
  sustainable ~38 fps (decode-bound; the 13.5 ms DMA push overlaps on the other core).
  So **24–30 fps clips hold steady**; higher `-q:v` quality → bigger frames → slower
  decode → lower ceiling. Set the rate via the AVI's encoded fps (vidprep/ffmpeg `fps=`);
  the player reads `avih` and paces to it. No HW
  video decoder on the S3 → clips must be **Motion-JPEG in an AVI**, pre-cropped to exactly
  172×320 (no on-device scale/crop). Lib: **JPEGDEC 1.8.4** (also pulled in bb_spi_lcd as
  a dep). SD on **SDMMC 4-bit** (pins in board.h, 1-bit fallback). Realistic ~15–25 fps.
  - ⚠️ **Video mode requires PSRAM ON.** Build it with `PSRAM=opi` (the N16R8 has octal
    PSRAM), NOT the effects-only `PSRAM=disabled` FQBN. The framebuffers + JPEG buffer
    live in PSRAM in this build (see Memory below). The committed effects-only build still
    uses `PSRAM=disabled` and keeps framebuffers in fast SRAM at 91 fps.
  - `video.h`/`video.cpp`: mounts SD, scans `/` for `*.avi`, demuxes the AVI `movi`
    list, decodes each `00dc` JPEG with JPEGDEC straight into the framebuffer (pixel
    type **RGB565_BIG_ENDIAN** = the sprite's byte-swapped layout). Paced to the clip's
    `avih` fps so producing one frame per call == playback at that rate.
  - **Scene model (genart.ino):** a scene is an effect (0..NUM_EFFECTS-1) OR a clip
    (NUM_EFFECTS..). BOOT cycles the unified counter, so it steps through effects *and*
    clips. Video scenes light the RGB LED cyan. Render-task stack bumped to 16 KB (SD +
    decode run there, on core 0).
  - ⚠️ **SD clip discovery on large (≥64 GB SDXC) cards:** the card mounts and reports
    correct total/used bytes, but `SD_MMC.open("/").openNextFile()` returns **nothing** —
    ESP-IDF FATFS won't enumerate the root on big FAT32 volumes. Reading a file **by path**
    works fine, though. So `videoInit()` does: (1) try `openNextFile()` enumeration (works
    on ≤32 GB cards), then (2) if that finds 0 clips, read a **`/clips.txt` manifest** —
    one clip filename/path per line (`#` comments, blank lines, optional leading `/`) — and
    open each listed clip **directly by path**. ✅ Confirmed working with a 64 GB SDXC card
    + 2-line manifest. A ≤32 GB FAT32 card enumerates normally and needs no manifest.
  - ⚠️ The device's FATFS is **FAT32 only — NOT exFAT** (exFAT is disabled in the Arduino
    esp32 core). 64 GB cards default to exFAT and won't mount; reformat FAT32 (use Rufus/
    guiformat for >32 GB, big cluster = faster PC mount), or just use a ≤32 GB card.
  - ⚠️ **Memory — RESOLVED 2026-06-02, the hard part of this feature.** Internal SRAM
    (512 KB, ~320–380 KB usable as heap) cannot hold **two 110 KB framebuffers + the
    16 KB render-task stack + the video libs' buffers** (SD_MMC/FS/JPEGDEC add ~20 KB
    static vs the effects-only build). Symptoms we hit, in order:
    - `freeHeap` looked OK (~30 KB) but the **largest contiguous block was only ~7.6 KB**
      → `xTaskCreatePinnedToCore` for the 16 KB render stack failed (`rc=-1`), the
      producer never ran, `loop()` blocked forever on `readyQ` → **black screen, no fps
      lines**. (Lesson: it's *contiguity/fragmentation*, not total free heap. Print
      `ESP.getMaxAllocHeap()`, and **check the task-create return code** — the old code
      ignored it.)
    - Allocating the task first fixed that, but then the **2nd framebuffer** had no room
      → `createSprite` returned NULL → render task wrote through NULL → `StoreProhibited`
      panic (`EXCVADDR 0x0`), reboot loop.
    - **Fix:** put both framebuffers in PSRAM (`sprite.setPsram(true)` before
      `createSprite`) and the 48 KB JPEG buffer in PSRAM (`ps_malloc`, falls back to
      `malloc`). Frees ~263 KB of internal SRAM. Sand then runs **~75 fps** (vs 91 with
      SRAM framebuffers): per-pixel render rose only ~2.4→3.4 ms (sand grid stays in
      SRAM; only the block-fill writes hit PSRAM), and the DMA push rose 11→13.5 ms — the
      push is the bottleneck, so the hit is small. The committed effects-only build keeps
      framebuffers in SRAM for the full 91 fps.
    - JPEG-buffer ceiling is `MAX_JPEG` (48 KB) — clips whose frames exceed it skip.
  - Clips are prepped on a computer. `tools/vidprep/` is a single-file static web app
    (open `index.html`, no server) to visually crop (locked 172:320), trim, and preview
    with a **nearest-neighbour** blow-up; it emits the exact `ffmpeg` command to render
    the AVI (pipeline: rotate → crop → scale 172×320 → fps; `-an`, MJPEG via `-q:v`).
- More sims (water/walls, Game of Life), optional PWM brightness via core-3.x
  `ledcAttach`, microSD presets.

## Conventions
- Each sketch in its own folder (Arduino requires `foo/foo.ino`). `genart/` is the app
  (multiple files compiled together); the others are single-file diagnostics.
- `board.h` is the shared, verified hardware config — reuse it; don't re-derive pins.
