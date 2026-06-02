// RGB LED test for Waveshare ESP32-S3-LCD-1.47.
// Official board variant confirms the onboard WS2812 RGB LED is on GPIO38.
// Cycles red -> green -> blue -> off. If the little LED visibly cycles colors,
// we have confirmed IO control and the correct pin family for this board.

#define RGB_PIN 38

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println("\n[rgb_test] cycling onboard RGB LED on GPIO38");
}

void loop() {
  // rgbLedWrite is provided by the ESP32 Arduino core (drives WS2812 via RMT).
  rgbLedWrite(RGB_PIN, 40, 0, 0);  Serial.println("RED");   delay(700);
  rgbLedWrite(RGB_PIN, 0, 40, 0);  Serial.println("GREEN"); delay(700);
  rgbLedWrite(RGB_PIN, 0, 0, 40);  Serial.println("BLUE");  delay(700);
  rgbLedWrite(RGB_PIN, 0, 0, 0);   Serial.println("OFF");   delay(700);
}
