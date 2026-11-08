// 03 · Melodía en la bocina (GPIO12). IMPORTANTE: switch SW1 en la posición "BOCINA".
const int BOCINA = 12;
const int notas[]    = {523, 587, 659, 698, 784, 880, 988, 1047};  // Do Re Mi Fa Sol La Si Do
const int duracion[] = {250, 250, 250, 250, 250, 250, 250, 500};

void setup() {
  ledcAttach(BOCINA, 2000, 8);
  for (int i = 0; i < 8; i++) {
    ledcWriteTone(BOCINA, notas[i]);
    delay(duracion[i]);
    ledcWriteTone(BOCINA, 0);            // silencio corto entre notas
    delay(40);
  }
}

void loop() {}
