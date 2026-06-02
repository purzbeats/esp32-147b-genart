// Backlight isolation test for Waveshare ESP32-S3-LCD-1.47.
// No display driver at all. Just toggles the presumed backlight pin (GPIO48)
// once per second. If the panel visibly glows (uniform grey/white, maybe with
// garbage) ON for 1s then OFF for 1s, the backlight pin + polarity are correct
// and the problem is in the SPI/display init. If it NEVER changes, GPIO48 is
// not the backlight (or polarity inverted) and we sweep candidate pins.

#define BL_PIN 48

void setup() {
  Serial.begin(115200);
  delay(200);
  pinMode(BL_PIN, OUTPUT);
  Serial.printf("\n[bl_test] toggling GPIO%d every 1s\n", BL_PIN);
}

void loop() {
  static bool on = false;
  on = !on;
  digitalWrite(BL_PIN, on ? HIGH : LOW);
  Serial.printf("[bl_test] backlight pin -> %s\n", on ? "HIGH (on?)" : "LOW (off?)");
  delay(1000);
}
