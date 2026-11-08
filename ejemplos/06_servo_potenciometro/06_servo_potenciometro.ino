// 06 · Servo (GPIO32) que sigue al potenciómetro (GPIO36).
// Requiere alimentar los motores: cable de H3 (5V y GND) a la bornera P2.
// Se usan pulsos por software (50 Hz) para no chocar con otros PWM de la placa.
const int SERVO = 32, POT = 36;

void pulsoServo(int grados) {
  int us = map(grados, 0, 180, 500, 2400);   // 0.5 ms = 0°, 2.4 ms = 180°
  digitalWrite(SERVO, HIGH); delayMicroseconds(us);
  digitalWrite(SERVO, LOW);  delayMicroseconds(20000 - us);
}

void setup() {
  pinMode(SERVO, OUTPUT);
}

void loop() {
  int grados = map(analogRead(POT), 0, 4095, 0, 180);
  pulsoServo(grados);   // un pulso cada 20 ms = 50 Hz
}
