// video.cpp — microSD MJPEG/AVI player. See video.h for the contract.
//
// AVI is just RIFF: a 'hdrl' LIST (with an 'avih' header giving us the frame rate)
// followed by a 'movi' LIST of per-frame chunks ('00dc' = a whole JPEG). We scan to
// 'movi', then walk it chunk-by-chunk, handing each JPEG to JPEGDEC, which decodes
// straight into the framebuffer. Clips are pre-sized to the screen by tools/vidprep/,
// so no on-device scaling/cropping is needed.
#include "video.h"
#include "board.h"
#include "SD_MMC.h"
#include "FS.h"
#include <JPEGDEC.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#define MAX_CLIPS 16
#define MAX_JPEG  (48 * 1024)        // per-frame JPEG ceiling (172x320 @ q7 is ~10-30KB)

static char  s_paths[MAX_CLIPS][80];
static char  s_names[MAX_CLIPS][40];
static int   s_count = 0;

static JPEGDEC  jpeg;
static File     s_file;
static int      s_curClip = -1;
static uint32_t s_moviStart = 0, s_moviEnd = 0, s_moviPos = 0;
static uint32_t s_frameUs   = 50000;     // per-frame interval (from avih); default 20fps
static uint32_t s_lastUs    = 0;
static uint32_t s_decodeUs  = 0;          // last raw SD-read + JPEG-decode time (no pacing)
static uint8_t* s_jpeg      = nullptr;

// Decode target for the JPEGDEC callback (single-threaded: render task only).
static uint16_t* s_tgt = nullptr;
static int       s_tgtW = 0, s_tgtH = 0;

// --- little helpers --------------------------------------------------------------
static uint32_t rdU32() { uint8_t b[4]; if (s_file.read(b, 4) != 4) return 0;
  return (uint32_t)b[0] | ((uint32_t)b[1] << 8) | ((uint32_t)b[2] << 16) | ((uint32_t)b[3] << 24); }
static bool rdTag(char t[5]) { t[4] = 0; return s_file.read((uint8_t*)t, 4) == 4; }
static bool tagEq(const char* a, const char* b) { return a[0]==b[0]&&a[1]==b[1]&&a[2]==b[2]&&a[3]==b[3]; }

// JPEGDEC draws MCU blocks; copy each into the framebuffer (RGB565 big-endian == the
// byte-swapped layout LovyanGFX sprites and the effects use, so a straight copy works).
static int jpegDraw(JPEGDRAW* p) {
  for (int r = 0; r < p->iHeight; r++) {
    int y = p->y + r;
    if (y < 0 || y >= s_tgtH) continue;
    int w = p->iWidth; int x = p->x;
    if (x + w > s_tgtW) w = s_tgtW - x;
    if (w > 0) memcpy(s_tgt + y * s_tgtW + x, p->pPixels + r * p->iWidth, w * 2);
  }
  return 1;
}

// --- AVI structure ---------------------------------------------------------------
static bool parseAvi() {
  s_file.seek(0);
  char t[5];
  if (!rdTag(t) || !tagEq(t, "RIFF")) return false;
  rdU32();                                   // file size
  if (!rdTag(t) || !tagEq(t, "AVI ")) return false;

  s_moviStart = 0; s_frameUs = 50000;
  uint32_t sz = s_file.size();
  while (s_file.position() + 8 <= sz) {
    uint32_t pos = s_file.position();
    if (!rdTag(t)) break;
    uint32_t csz = rdU32();
    if (tagEq(t, "LIST")) {
      char lt[5]; if (!rdTag(lt)) break;
      if (tagEq(lt, "movi")) { s_moviStart = pos + 12; s_moviEnd = pos + 8 + csz; break; }
      if (tagEq(lt, "hdrl")) continue;       // descend: its children are read next
      s_file.seek(pos + 8 + csz + (csz & 1));// skip strl/odml/etc as a block
    } else if (tagEq(t, "avih")) {
      uint32_t usec = rdU32();               // dwMicroSecPerFrame
      if (usec) s_frameUs = usec;
      s_file.seek(pos + 8 + csz + (csz & 1));
    } else {
      s_file.seek(pos + 8 + csz + (csz & 1));// JUNK, strh, strf, ...
    }
  }
  if (!s_moviStart) return false;
  if (s_frameUs < 16000)  s_frameUs = 16000;   // cap ~60fps
  if (s_frameUs > 500000) s_frameUs = 500000;  // floor ~2fps
  s_moviPos = s_moviStart;
  return true;
}

// Read the next JPEG frame into s_jpeg; returns its size, or 0 on hard failure.
// Loops back to the first frame at end-of-clip.
static uint32_t nextFrame() {
  bool looped = false;
  char t[5];
  for (;;) {
    if (s_moviPos + 8 > s_moviEnd) {         // end of movi -> loop once
      if (looped) return 0;
      looped = true; s_moviPos = s_moviStart; continue;
    }
    s_file.seek(s_moviPos);
    if (!rdTag(t)) return 0;
    uint32_t csz = rdU32();
    uint32_t adv = 8 + csz + (csz & 1);
    if (tagEq(t, "LIST")) { s_moviPos += 12; continue; }   // 'rec ' grouping: descend
    bool isVideo = (t[2] == 'd' && (t[3] == 'c' || t[3] == 'b'));
    if (isVideo && csz > 0 && csz <= MAX_JPEG && s_jpeg) {
      if (s_file.read(s_jpeg, csz) != (int)csz) { s_moviPos += adv; continue; }
      s_moviPos += adv;
      return csz;
    }
    s_moviPos += adv;                         // audio / oversize / index -> skip
  }
}

static bool openClip(int clip) {
  if (s_file) s_file.close();
  s_curClip = -1;
  s_file = SD_MMC.open(s_paths[clip], FILE_READ);
  if (!s_file) return false;
  if (!parseAvi()) { s_file.close(); return false; }
  s_curClip = clip; s_lastUs = 0;
  return true;
}

// --- public API ------------------------------------------------------------------
static bool endsAvi(const char* n) {
  int L = strlen(n); if (L < 4) return false;
  const char* e = n + L - 4;
  return e[0]=='.' && (e[1]|32)=='a' && (e[2]|32)=='v' && (e[3]|32)=='i';
}

int videoInit() {
  s_count = 0;
  SD_MMC.setPins(PIN_SD_CLK, PIN_SD_CMD, PIN_SD_D0, PIN_SD_D1, PIN_SD_D2, PIN_SD_D3);
  if (!SD_MMC.begin("/sdcard", false)) {       // 4-bit
    SD_MMC.setPins(PIN_SD_CLK, PIN_SD_CMD, PIN_SD_D0);
    if (!SD_MMC.begin("/sdcard", true)) {      // 1-bit fallback
      Serial.println("[video] no SD card / mount failed");
      return 0;
    }
  }
  // The JPEG buffer is a sequential read/decode source, not a per-pixel hot path,
  // so it goes in PSRAM (frees ~48 KB of scarce internal SRAM for the framebuffers
  // and render stack). Fall back to internal RAM if PSRAM is unavailable.
  const char* jpegMem = "PSRAM";
  if (!s_jpeg) s_jpeg = (uint8_t*)ps_malloc(MAX_JPEG);
  if (!s_jpeg) { jpegMem = "internal RAM"; s_jpeg = (uint8_t*)malloc(MAX_JPEG); }
  if (!s_jpeg) Serial.println("[video] JPEG buffer alloc failed");
  else         Serial.printf("[video] JPEG buf in %s\n", jpegMem);

  // Card identity + usage: distinguishes "file never landed" (used~0) from an
  // enumeration bug (used reflects the file but openNextFile sees nothing).
  Serial.printf("[video] card type=%d  size=%lluMB  total=%lluMB  used=%lluMB\n",
                (int)SD_MMC.cardType(),
                SD_MMC.cardSize()   / (1024ULL * 1024ULL),
                SD_MMC.totalBytes() / (1024ULL * 1024ULL),
                SD_MMC.usedBytes()  / (1024ULL * 1024ULL));

  File root = SD_MMC.open("/");
  if (root) {
    for (File f = root.openNextFile(); f && s_count < MAX_CLIPS; f = root.openNextFile()) {
      Serial.printf("[video]   entry: name='%s' path='%s' dir=%d size=%u\n",
                    f.name(), f.path(), (int)f.isDirectory(), (unsigned)f.size());
      if (!f.isDirectory() && endsAvi(f.name())) {
        strncpy(s_paths[s_count], f.path(), sizeof(s_paths[0]) - 1);
        strncpy(s_names[s_count], f.name(), sizeof(s_names[0]) - 1);
        s_count++;
      }
      f.close();
    }
    root.close();
  } else {
    Serial.println("[video] could not open root '/'");
  }
  // Large SDXC cards (e.g. 64 GB) mount fine but their root directory often won't
  // enumerate via openNextFile() on ESP-IDF FATFS — reading a file BY PATH still works,
  // though. So if the scan found nothing, fall back to opening known clip paths directly.
  // Manifest fallback: /clips.txt, one clip filename (or path) per line. Large SDXC
  // cards mount but their root won't enumerate, yet opening BY PATH works — so we read
  // the manifest directly and open each listed clip directly. Lines starting with '#'
  // are comments; blank lines ignored; a leading '/' is optional.
  if (s_count == 0) {
    File mf = SD_MMC.open("/clips.txt", FILE_READ);
    if (!mf) {
      Serial.println("[video] no /clips.txt manifest");
    } else {
      while (mf.available() && s_count < MAX_CLIPS) {
        String line = mf.readStringUntil('\n');
        line.trim();                                   // strip CR / surrounding space
        if (line.length() == 0 || line[0] == '#') continue;
        char path[80];
        snprintf(path, sizeof(path), "%s%s", line[0] == '/' ? "" : "/", line.c_str());
        File f = SD_MMC.open(path, FILE_READ);
        if (f && !f.isDirectory()) {
          Serial.printf("[video] manifest clip: %s (%u bytes)\n", path, (unsigned)f.size());
          f.close();
          strncpy(s_paths[s_count], path, sizeof(s_paths[0]) - 1);
          const char* nm = strrchr(path, '/'); nm = nm ? nm + 1 : path;
          strncpy(s_names[s_count], nm, sizeof(s_names[0]) - 1);
          s_count++;
        } else {
          Serial.printf("[video] manifest MISSING: %s\n", path);
          if (f) f.close();
        }
      }
      mf.close();
    }
  }

  Serial.printf("[video] SD mounted, %d clip(s) found\n", s_count);
  for (int i = 0; i < s_count; i++) Serial.printf("[video]   %s\n", s_names[i]);
  return s_count;
}

int  videoCount() { return s_count; }
const char* videoName(int clip) { return (clip >= 0 && clip < s_count) ? s_names[clip] : ""; }

void videoRenderFrame(uint16_t* buf, int w, int h, int clip) {
  if (clip < 0 || clip >= s_count || !s_jpeg) { memset(buf, 0, w * h * 2); return; }
  if (clip != s_curClip && !openClip(clip))   { memset(buf, 0, w * h * 2); return; }

  // Pace to the clip's own frame rate: producing one frame per call == that fps.
  uint32_t now = micros();
  if (s_lastUs) {
    int32_t due = (int32_t)(s_frameUs - (now - s_lastUs));
    if (due > 1500) vTaskDelay(pdMS_TO_TICKS(due / 1000));
  }
  s_lastUs = micros();

  // Raw cost of SD read + JPEG decode (NOT including the pacing delay above). This is
  // the real budget that bounds max sustainable fps: if it exceeds the frame interval
  // (e.g. 41.6 ms at 24 fps) the clip can't hold its encoded rate.
  uint32_t d0 = micros();
  uint32_t sz = nextFrame();
  if (!sz) { memset(buf, 0, w * h * 2); s_decodeUs = micros() - d0; return; }

  s_tgt = buf; s_tgtW = w; s_tgtH = h;
  if (jpeg.openRAM(s_jpeg, sz, jpegDraw)) {
    jpeg.setPixelType(RGB565_BIG_ENDIAN);     // matches the sprite's byte-swapped 565
    jpeg.decode(0, 0, 0);
    jpeg.close();
  }
  s_decodeUs = micros() - d0;
}

uint32_t videoLastDecodeUs() { return s_decodeUs; }
