// 05 · Medidor de distancia: HC-SR04 (Trig 15, Echo 2) en el display de 7 segmentos (TM1637: CLK 27, DIO 14).
// Librería: TM1637 (Avishay Orpaz).
#include <TM1637Display.h>

const int TRIG = 15, ECHO = 2;   // ojo: el manual dice Echo=12, el pin real es 2
TM1637Display display(27, 14);

float medirCm() {
  digitalWrite(TRIG, LOW);  delayMicroseconds(4);
  digitalWrite(TRIG, HIGH); delayMicroseconds(10);
  digitalWrite(TRIG, LOW);
  unsigned long us = pulseIn(ECHO, HIGH, 30000);  // máx. ~5 m
  return us ? us * 0.0343 / 2 : -1;
}

void setup() {
  Serial.begin(115200);
  pinMode(TRIG, OUTPUT);
  pinMode(ECHO, INPUT);
  display.setBrightness(5);
}

void loop() {
  float d = medirCm();
  Serial.printf("Distancia: %.1f cm\n", d);
  if (d > 0) display.showNumberDec((int)d); else display.showNumberDec(-1);
  delay(150);
}
