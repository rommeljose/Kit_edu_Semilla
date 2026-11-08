// 02 · El potenciómetro (GPIO36) controla el brillo del LED verde (GPIO23) con PWM.
const int POT = 36, LED_VERDE = 23;

void setup() {
  Serial.begin(115200);
  ledcAttach(LED_VERDE, 5000, 8);        // PWM de 5 kHz, 8 bits (0-255)
}

void loop() {
  int valor = analogRead(POT);           // 0 a 4095
  int brillo = map(valor, 0, 4095, 0, 255);
  ledcWrite(LED_VERDE, brillo);
  Serial.printf("Potenciometro: %4d  ->  brillo %3d\n", valor, brillo);
  delay(100);
}
