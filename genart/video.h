// video.h — microSD MJPEG/AVI player for the gen-art pipeline.
//
// The ESP32-S3 has no hardware video decoder, so clips must be Motion-JPEG in an
// AVI container, pre-cropped to exactly SCREEN_W x SCREEN_H (use tools/vidprep/).
// Each clip becomes its own "scene" after the procedural EFFECTS[] (see genart.ino),
// so the BOOT button steps through effects AND clips with one mechanism.
//
// All calls happen on the render task (core 0) — same place effects run — so SD I/O
// and JPEG decode never touch the push core. Decode is paced to the clip's own frame
// rate (from the AVI header), so producing one frame per call == playback at that fps.
#pragma once
#include <stdint.h>

// Mount the SD card (SDMMC 4-bit) and scan the root for *.avi clips.
// Returns the number of clips found (0 if no card / no clips / mount failed).
int  videoInit();
int  videoCount();
const char* videoName(int clip);          // basename of clip i, "" if out of range
uint32_t videoLastDecodeUs();             // raw SD-read + decode time of the last frame (no pacing)

// Decode the next frame of `clip` into `buf` (SCREEN_W*SCREEN_H, byte-swapped RGB565,
// same layout the effects write). Selecting a new clip (or first call) opens/seeks it.
// Paces itself to the clip's fps and loops at end. Safe no-op (clears buf) on error.
void videoRenderFrame(uint16_t* buf, int w, int h, int clip);
