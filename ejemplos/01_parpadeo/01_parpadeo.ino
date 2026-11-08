// 01 · Parpadeo: el "Hola mundo" de la electrónica. LED azul en GPIO19.
const int LED_AZUL = 19;

void setup() {
  pinMode(LED_AZUL, OUTPUT);
}

void loop() {
  digitalWrite(LED_AZUL, HIGH);  // encender
  delay(500);
  digitalWrite(LED_AZUL, LOW);   // apagar
  delay(500);
}
