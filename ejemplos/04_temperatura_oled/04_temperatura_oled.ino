// 04 · Termómetro: sensor DS18B20 (GPIO4) mostrado en la pantalla OLED (I2C 21/22).
// Librerías: OneWire, DallasTemperature, Adafruit SSD1306, Adafruit GFX.
#include <Wire.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

OneWire oneWire(4);
DallasTemperature sensor(&oneWire);
Adafruit_SSD1306 oled(128, 64, &Wire, -1);

void setup() {
  Serial.begin(115200);
  Wire.begin(21, 22);
  oled.begin(SSD1306_SWITCHCAPVCC, 0x3C);
  sensor.begin();
}

void loop() {
  sensor.requestTemperatures();
  float t = sensor.getTempCByIndex(0);
  Serial.printf("Temperatura: %.2f C\n", t);

  oled.clearDisplay();
  oled.setTextColor(SSD1306_WHITE);
  oled.setTextSize(1); oled.setCursor(0, 0);  oled.print("Temperatura");
  oled.setTextSize(3); oled.setCursor(0, 24); oled.printf("%.1f", t);
  oled.setTextSize(2); oled.print(" C");
  oled.display();
  delay(1000);
}
