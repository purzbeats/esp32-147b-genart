// sd_reader — temporary "USB card-reader mode" for the ESP32-S3-LCD-1.47B.
//
// Exposes the onboard microSD (SDMMC 4-bit) to the host PC as a USB Mass-Storage
// drive, so you can drag clips onto the card without pulling it. Flash this, copy
// files, then flash genart back. The MSC read/write callbacks just proxy raw 512B
// sectors to/from SD_MMC.
//
// SD pins mirror board.h (CLK14 CMD15 D0..D3 = 16/18/17/21).
#include "USB.h"
#include "USBMSC.h"
#include "SD_MMC.h"

#define PIN_SD_CLK 14
#define PIN_SD_CMD 15
#define PIN_SD_D0  16
#define PIN_SD_D1  18
#define PIN_SD_D2  17
#define PIN_SD_D3  21

USBMSC msc;
static uint32_t g_secSize = 512;

static int32_t onRead(uint32_t lba, uint32_t offset, void* buffer, uint32_t bufsize) {
  uint32_t n = bufsize / g_secSize;
  uint8_t* p = (uint8_t*)buffer;
  for (uint32_t i = 0; i < n; i++)
    if (!SD_MMC.readRAW(p + i * g_secSize, lba + i)) return -1;
  return bufsize;
}
static int32_t onWrite(uint32_t lba, uint32_t offset, uint8_t* buffer, uint32_t bufsize) {
  uint32_t n = bufsize / g_secSize;
  for (uint32_t i = 0; i < n; i++)
    if (!SD_MMC.writeRAW(buffer + i * g_secSize, lba + i)) return -1;
  return bufsize;
}
static bool onStartStop(uint8_t power_condition, bool start, bool load_eject) { return true; }

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println("\n[sd_reader] booting");

  SD_MMC.setPins(PIN_SD_CLK, PIN_SD_CMD, PIN_SD_D0, PIN_SD_D1, PIN_SD_D2, PIN_SD_D3);
  bool ok = SD_MMC.begin("/sdcard", false);                 // 4-bit
  if (!ok) { SD_MMC.setPins(PIN_SD_CLK, PIN_SD_CMD, PIN_SD_D0);
             ok = SD_MMC.begin("/sdcard", true); }           // 1-bit fallback
  if (!ok) { Serial.println("[sd_reader] SD mount FAILED — card seated?"); return; }

  g_secSize = SD_MMC.sectorSize();
  uint32_t nsec = SD_MMC.numSectors();
  Serial.printf("[sd_reader] SD ok: %lu sectors x %lu B = %.1f MB\n",
                (unsigned long)nsec, (unsigned long)g_secSize, nsec * (float)g_secSize / 1048576.0f);

  msc.vendorID("ESP32S3");
  msc.productID("LCD147B SD");
  msc.productRevision("1.0");
  msc.onRead(onRead);
  msc.onWrite(onWrite);
  msc.onStartStop(onStartStop);
  msc.mediaPresent(true);
  msc.begin(nsec, g_secSize);
  USB.begin();

  Serial.println("[sd_reader] USB mass storage up — card should appear on the PC");
}

void loop() { delay(1000); }
