// 07 · Arcoíris animado en la matriz RGB 4x4 (GPIO13, 16 LED direccionables).
// Librería: Adafruit NeoPixel. La matriz va en zigzag: fila 0 izq->der, fila 1 der->izq...
#include <Adafruit_NeoPixel.h>

Adafruit_NeoPixel matriz(16, 13, NEO_GRB + NEO_KHZ800);

int indice(int x, int y) { return (y % 2 == 0) ? y * 4 + x : y * 4 + (3 - x); }

void setup() {
  matriz.begin();
  matriz.setBrightness(40);   // 0-255: no exagerar, consume y deslumbra
}

void loop() {
  static uint16_t tono = 0;
  for (int y = 0; y < 4; y++)
    for (int x = 0; x < 4; x++)
      matriz.setPixelColor(indice(x, y), matriz.gamma32(matriz.ColorHSV(tono + (x + y) * 6000)));
  matriz.show();
  tono += 400;
  delay(20);
}
