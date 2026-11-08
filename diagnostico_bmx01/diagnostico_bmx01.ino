/*
 * =====================================================================
 *  DIAGNÓSTICO Y PRUEBA – Placa BMX-01 (Kit Semilleros Científicos)
 *  ESP32-WROOM-32D · Mincyt Venezuela
 * =====================================================================
 *  Monitor serie: 115200 baudios. Escribe una letra/número + Enter:
 *
 *   h  ayuda (este menú)          a  diagnóstico automático completo
 *   0  info del chip              i  escaneo bus I2C
 *   1  LEDs (azul/verde/rojo/amarillo)
 *   2  bocina (melodía)           3  relé (clic-clic)
 *   4  matriz RGB 4x4             5  pantalla OLED
 *   6  display 7 segmentos        7  lectura de sensores
 *   8  motor DC                   9  motor paso a paso
 *   s  servomotor                 w  escaneo WiFi
 *   b  escaneo Bluetooth (BLE)    m  modo monitor en vivo (on/off)
 *   p  punto de acceso WiFi con página web de control
 *   n  nombres ROMMEL / FIDIA rotativos (matriz + 7 seg)
 *   e  sensor de estacionamiento (ultrasonido + bocina)
 *   c  calibrar botones        j  juego con la cruz de botones
 *
 *  Notas de hardware:
 *   - LED rojo (GPIO1) y amarillo (GPIO3) comparten el puerto serie:
 *     durante su prueba el Serial se apaga ~3 s.
 *   - Relé en GPIO0 (pin de arranque): solo se usa después del boot.
 *   - GPIO34 lo comparten la cruz de botones y el receptor IR: se
 *     elige con el switch SW2 ("BOTONES" / "RECEPTOR INFRARROJO").
 *   - Bocina en GPIO12 solo con SW1 en posición BOCINA. La señal del servo
 *     (GPIO32) se acopla débilmente a la bocina y produce un zumbido bajo.
 *   - Motores: llevar 5V y GND del cabezal H3 a la bornera P2.
 *   - Echo del ultrasonido en GPIO2 (el manual dice 12).
 * =====================================================================
 */
#include <Wire.h>
#include <WiFi.h>
#include <WebServer.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Adafruit_NeoPixel.h>
#include <TM1637Display.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <BLEDevice.h>
#include <BLEScan.h>
#include <IRremote.hpp>
#include <Preferences.h>

// ---------------- Mapa de pines (leyenda de la placa) ----------------
const int LED_AZUL = 19, LED_VERDE = 23, LED_ROJO = 1, LED_AMARILLO = 3;
const int BOCINA = 12;           // con el switch SW1 en BOCINA (no en LIBRE)
const int RELE = 0;
const int MATRIZ_RGB = 13, N_PIXELES = 16;
const int OLED_SDA = 21, OLED_SCL = 22;
const int SEG_CLK = 27, SEG_DIO = 14;
const int TEMP_PIN = 4;
const int INCLINACION = 39;
const int LUZ = 35;
const int POT = 36;
const int US_TRIG = 15;
int usEcho = 2;                     // la placa dice 2, el manual dice 12
const int BOTONES_IR = 34;
const int STEP_PINS[4] = {16, 17, 5, 18};
const int MDC_EN = 33, MDC_IN1 = 25, MDC_IN2 = 26;
const int SERVO_PIN = 32;

// ---------------- Objetos ----------------
Adafruit_SSD1306 oled(128, 64, &Wire, -1);
Adafruit_NeoPixel pix(N_PIXELES, MATRIZ_RGB, NEO_GRB + NEO_KHZ800);
TM1637Display seg(SEG_CLK, SEG_DIO);
OneWire ow(TEMP_PIN);
DallasTemperature ds(&ow);
WebServer web(80);

bool oledOk = false, dsOk = false, monitorOn = false, apOn = false, irOn = false;
uint8_t oledAddr = 0;
int okCount = 0, warnCount = 0;

// ---------------- Utilidades ----------------
void titulo(const char *t) {
  Serial.println();
  Serial.println(F("--------------------------------------------------"));
  Serial.printf(">> %s\n", t);
  Serial.println(F("--------------------------------------------------"));
}
void ok(const char *msg)   { okCount++;   Serial.printf("  [OK]    %s\n", msg); }
void warn(const char *msg) { warnCount++; Serial.printf("  [AVISO] %s\n", msg); }

void oledMsg(const String &l1, const String &l2 = "", const String &l3 = "") {
  if (!oledOk) return;
  oled.clearDisplay();
  oled.setTextColor(SSD1306_WHITE);
  oled.setTextSize(1);
  oled.setCursor(0, 0);  oled.println(F("BMX-01 Diagnostico"));
  oled.drawLine(0, 10, 127, 10, SSD1306_WHITE);
  oled.setCursor(0, 16); oled.println(l1);
  oled.setCursor(0, 32); oled.println(l2);
  oled.setCursor(0, 48); oled.println(l3);
  oled.display();
}

void beep(int f, int ms) {
  ledcWriteTone(BOCINA, f);
  delay(ms);
  ledcWriteTone(BOCINA, 0);
}

// ---------------- Lecturas de sensores ----------------
float leerTemperatura(bool &valida, String &fuente) {
  valida = false;
  if (dsOk) {
    ds.requestTemperatures();
    float t = ds.getTempCByIndex(0);
    if (t > -55 && t < 125) { valida = true; fuente = "DS18B20"; return t; }
  }
  if (!apOn) {  // GPIO4 es ADC2: no se puede leer analógico con WiFi activo
    uint32_t mv = analogReadMilliVolts(TEMP_PIN);
    fuente = "analogico " + String(mv) + " mV (LM35: 10mV/C)";
    valida = true;
    return mv / 10.0;
  }
  fuente = "no disponible con WiFi";
  return NAN;
}

float leerDistanciaCm(int echo) {
  digitalWrite(US_TRIG, LOW);  delayMicroseconds(4);
  digitalWrite(US_TRIG, HIGH); delayMicroseconds(10);
  digitalWrite(US_TRIG, LOW);
  unsigned long d = pulseIn(echo, HIGH, 30000);
  if (d == 0) return -1;
  return d * 0.0343 / 2.0;
}

const char *nombreBoton(int raw) {
  // Calibración preliminar: se ajusta tras medir cada botón
  if (raw < 100) return "ninguno/0";
  return "?";
}

// Pulsos por software a 50 Hz: no usa temporizadores LEDC, así no choca con
// la bocina (2 kHz) ni con el motor DC (1 kHz). 0° = 500 us, 180° = 2400 us.
void servoIr(int grados, int ms) {
  int us = map(constrain(grados, 0, 180), 0, 180, 500, 2400);
  pinMode(SERVO_PIN, OUTPUT);
  unsigned long t0 = millis();
  while (millis() - t0 < (unsigned long)ms) {
    digitalWrite(SERVO_PIN, HIGH); delayMicroseconds(us);
    digitalWrite(SERVO_PIN, LOW);  delayMicroseconds(20000 - us);
  }
}

// ---------------- Pruebas ----------------
void pruebaInfoChip() {
  titulo("0. Informacion del chip");
  Serial.printf("  Modelo: %s rev %d, %d nucleos @ %d MHz\n",
                ESP.getChipModel(), ESP.getChipRevision(), ESP.getChipCores(), ESP.getCpuFreqMHz());
  Serial.printf("  Flash: %u MB  | RAM libre: %u bytes | PSRAM: %u bytes\n",
                ESP.getFlashChipSize() / (1024 * 1024), ESP.getFreeHeap(), ESP.getPsramSize());
  Serial.printf("  MAC WiFi: %s\n", WiFi.macAddress().c_str());
  Serial.printf("  SDK: %s\n", ESP.getSdkVersion());
  Serial.printf("  Temp. interna aprox. del chip: %.1f C\n", temperatureRead());
  ok("Chip ESP32 responde");
}

void pruebaI2C() {
  titulo("i. Escaneo bus I2C (SDA 21, SCL 22)");
  int n = 0;
  for (uint8_t a = 1; a < 127; a++) {
    Wire.beginTransmission(a);
    if (Wire.endTransmission() == 0) {
      Serial.printf("  Dispositivo en 0x%02X%s\n", a, (a == 0x3C || a == 0x3D) ? "  (OLED SSD1306)" : "");
      if (!oledAddr && (a == 0x3C || a == 0x3D)) oledAddr = a;
      n++;
    }
  }
  if (n) ok("Bus I2C con dispositivos"); else warn("Ningun dispositivo I2C encontrado");
}

void pruebaLEDs() {
  titulo("1. LEDs: azul, verde, rojo, amarillo");
  Serial.println(F("  (el Serial se apaga ~3 s para usar GPIO1/GPIO3)"));
  oledMsg("Prueba LEDs", "Azul-Verde-Rojo-Amar.");
  Serial.flush();
  Serial.end();
  const int leds[4] = {LED_AZUL, LED_VERDE, LED_ROJO, LED_AMARILLO};
  for (int i = 0; i < 4; i++) pinMode(leds[i], OUTPUT);
  for (int r = 0; r < 2; r++)
    for (int i = 0; i < 4; i++) { digitalWrite(leds[i], HIGH); delay(300); digitalWrite(leds[i], LOW); }
  for (int i = 0; i < 4; i++) digitalWrite(leds[i], HIGH);
  delay(500);
  for (int i = 0; i < 4; i++) digitalWrite(leds[i], LOW);
  Serial.begin(115200);
  delay(50);
  Serial.println(F("  Serial restablecido."));
  ok("Secuencia LED ejecutada (verificar visualmente)");
}

void pruebaBocina() {
  titulo("2. Bocina (GPIO12, SW1 en BOCINA)");
  oledMsg("Prueba bocina", "Do-Re-Mi-Fa-Sol");
  const int notas[] = {523, 587, 659, 698, 784, 880, 988, 1047};
  for (int f : notas) beep(f, 150);
  ok("Melodia enviada (si no suena: SW1 en posicion BOCINA)");
}

void pruebaRele() {
  titulo("3. Rele (GPIO0)");
  oledMsg("Prueba rele", "ON / OFF x3");
  pinMode(RELE, OUTPUT);
  for (int i = 0; i < 3; i++) {
    digitalWrite(RELE, HIGH); Serial.println(F("  Rele: GPIO0=1")); delay(500);
    digitalWrite(RELE, LOW);  Serial.println(F("  Rele: GPIO0=0")); delay(500);
  }
  ok("Rele conmutado (debe oirse clic y ver su LED)");
}

void pruebaMatriz() {
  titulo("4. Matriz RGB 4x4 (GPIO13)");
  oledMsg("Matriz RGB", "Rojo-Verde-Azul", "Arcoiris");
  pix.setBrightness(40);
  uint32_t c[3] = {pix.Color(255, 0, 0), pix.Color(0, 255, 0), pix.Color(0, 0, 255)};
  for (int k = 0; k < 3; k++) { pix.fill(c[k]); pix.show(); delay(500); }
  pix.clear();
  for (int i = 0; i < N_PIXELES; i++) { pix.setPixelColor(i, pix.Color(255, 255, 255)); pix.show(); delay(60); }
  for (int j = 0; j < 256 * 2; j += 4) {
    for (int i = 0; i < N_PIXELES; i++) pix.setPixelColor(i, pix.gamma32(pix.ColorHSV((i * 65536L / N_PIXELES) + j * 256)));
    pix.show(); delay(10);
  }
  pix.clear(); pix.show();
  ok("Matriz RGB animada (verificar los 16 LEDs)");
}

void pruebaOLED() {
  titulo("5. Pantalla OLED");
  if (!oledOk) { warn("OLED no inicializada (no responde en 0x3C/0x3D)"); return; }
  oled.clearDisplay();
  oled.drawRect(0, 0, 128, 64, SSD1306_WHITE);
  oled.fillCircle(20, 32, 12, SSD1306_WHITE);
  oled.drawTriangle(45, 50, 60, 14, 75, 50, SSD1306_WHITE);
  oled.setTextSize(2); oled.setTextColor(SSD1306_WHITE);
  oled.setCursor(82, 10); oled.print("ESP");
  oled.setCursor(88, 34); oled.print("32");
  oled.display(); delay(1500);
  oled.invertDisplay(true);  delay(500);
  oled.invertDisplay(false);
  oledMsg("OLED OK", "128x64 I2C", "Direccion 0x" + String(oledAddr, HEX));
  ok("OLED dibujando graficos");
}

void prueba7Seg() {
  titulo("6. Display 7 segmentos TM1637 (CLK27, DIO14)");
  oledMsg("Display 7 seg", "8888 y conteo");
  seg.setBrightness(5);
  uint8_t todo[] = {0xff, 0xff, 0xff, 0xff};
  seg.setSegments(todo); delay(800);
  for (int i = 0; i <= 100; i += 5) { seg.showNumberDec(i * 99 / 10, false); delay(60); }
  seg.showNumberDecEx(1234, 0b01000000, false); delay(800);  // 12:34
  seg.clear();
  ok("Display 7 seg (verificar que todos los segmentos encienden)");
}

void pruebaSensores() {
  titulo("7. Sensores");
  bool v; String fuente;
  float t = leerTemperatura(v, fuente);
  Serial.printf("  Temperatura (GPIO4): %.1f C  [%s]\n", t, fuente.c_str());
  if (dsOk) ok("Sensor de temperatura DS18B20 detectado");
  else warn("Sin DS18B20 en 1-Wire; se muestra lectura analogica de GPIO4");

  int luz = analogRead(LUZ);
  Serial.printf("  Luz LDR (GPIO35): %d / 4095  (%u mV)\n", luz, analogReadMilliVolts(LUZ));
  int pot = analogRead(POT);
  Serial.printf("  Potenciometro (GPIO36): %d / 4095 (%d%%)\n", pot, pot * 100 / 4095);
  Serial.printf("  Inclinacion (GPIO39): %d\n", digitalRead(INCLINACION));
  int bot = analogRead(BOTONES_IR);
  Serial.printf("  Botones/IR (GPIO34): %d / 4095\n", bot);

  float d2 = leerDistanciaCm(2);
  float d12 = -1;   // ya confirmado: Echo en GPIO2 (no se toca GPIO12, es la bocina)
  Serial.printf("  Ultrasonico: Echo GPIO2 -> %.1f cm | Echo GPIO12 -> %.1f cm  (-1 = sin eco)\n", d2, d12);
  if (d2 > 0) { usEcho = 2; ok("Ultrasonico responde con Echo en GPIO2"); }
  else if (d12 > 0) { usEcho = 12; ok("Ultrasonico responde con Echo en GPIO12"); }
  else warn("Ultrasonico sin eco (apuntar a un objeto a 5-200 cm)");
  oledMsg("T:" + String(t, 1) + "C Luz:" + String(luz),
          "Pot:" + String(pot) + " Incl:" + String(digitalRead(INCLINACION)),
          "Dist:" + String(usEcho == 2 ? d2 : d12, 1) + " cm");
}

void stepperPaso(int s) {
  static const uint8_t seq[8] = {0b1000, 0b1100, 0b0100, 0b0110, 0b0010, 0b0011, 0b0001, 0b1001};
  for (int i = 0; i < 4; i++) digitalWrite(STEP_PINS[i], (seq[s & 7] >> (3 - i)) & 1);
}
void stepperOff() { for (int i = 0; i < 4; i++) digitalWrite(STEP_PINS[i], LOW); }

void pruebaStepper() {
  titulo("9. Motor paso a paso (ULN2003: 16,17,5,18)");
  oledMsg("Paso a paso", "1/4 vuelta ida", "1/4 vuelta vuelta");
  for (int i = 0; i < 4; i++) pinMode(STEP_PINS[i], OUTPUT);
  for (int s = 0; s < 1024; s++) { stepperPaso(s); delayMicroseconds(1200); }
  delay(300);
  for (int s = 1024; s > 0; s--) { stepperPaso(s); delayMicroseconds(1200); }
  stepperOff();
  ok("Secuencia enviada (los LEDs del ULN2003 parpadean; conectar el motor 28BYJ-48)");
}

void pruebaMotorDC() {
  titulo("8. Motor DC (L293: EN33, IN25, IN26)");
  oledMsg("Motor DC", "Adelante / Atras", "rampa PWM");
  pinMode(MDC_IN1, OUTPUT); pinMode(MDC_IN2, OUTPUT);
  ledcAttach(MDC_EN, 1000, 8);
  for (int dir = 0; dir < 2; dir++) {
    digitalWrite(MDC_IN1, dir == 0); digitalWrite(MDC_IN2, dir == 1);
    Serial.printf("  Sentido %s\n", dir == 0 ? "adelante" : "atras");
    for (int d = 80; d <= 255; d += 25) { ledcWrite(MDC_EN, d); delay(150); }
    delay(500);
    ledcWrite(MDC_EN, 0); delay(400);
  }
  digitalWrite(MDC_IN1, LOW); digitalWrite(MDC_IN2, LOW);
  ok("Motor DC: rampa en ambos sentidos (conectar motor en bornera MOTOR DC)");
}

void pruebaServo() {
  titulo("s. Servomotor (GPIO32)");
  oledMsg("Servomotor", "0 - 90 - 180 grados");
  int ang[] = {0, 90, 180, 90};
  for (int a : ang) { Serial.printf("  %d grados\n", a); servoIr(a, 800); }
  ok("Servo barrido");
}

void pruebaWiFi() {
  titulo("w. Escaneo de redes WiFi");
  oledMsg("Escaneando WiFi...");
  if (!apOn) WiFi.mode(WIFI_STA);
  int n = WiFi.scanNetworks();
  if (n <= 0) { warn("No se encontraron redes WiFi"); return; }
  for (int i = 0; i < n && i < 15; i++)
    Serial.printf("  %2d) %-32s %4d dBm  canal %2d  %s\n", i + 1, WiFi.SSID(i).c_str(),
                  WiFi.RSSI(i), WiFi.channel(i), WiFi.encryptionType(i) == WIFI_AUTH_OPEN ? "abierta" : "segura");
  oledMsg("WiFi OK", String(n) + " redes", "Mejor: " + WiFi.SSID(0).substring(0, 14));
  WiFi.scanDelete();
  if (!apOn) WiFi.mode(WIFI_OFF);
  ok("Radio WiFi funcionando");
}

void pruebaBLE() {
  titulo("b. Escaneo Bluetooth Low Energy (5 s)");
  oledMsg("Escaneando BLE...", "5 segundos");
  BLEDevice::init("BMX-01");
  BLEScan *sc = BLEDevice::getScan();
  sc->setActiveScan(true);
  BLEScanResults *r = sc->start(5, false);
  int n = r->getCount();
  for (int i = 0; i < n && i < 15; i++) {
    BLEAdvertisedDevice d = r->getDevice(i);
    Serial.printf("  %s  %4d dBm  %s\n", d.getAddress().toString().c_str(), d.getRSSI(),
                  d.haveName() ? d.getName().c_str() : "");
  }
  Serial.printf("  Dispositivos BLE encontrados: %d\n", n);
  sc->clearResults();
  oledMsg("Bluetooth OK", String(n) + " dispositivos BLE");
  ok("Radio Bluetooth funcionando");
}

// =====================================================================
//  NOMBRES ROTATIVOS: matriz RGB 4x4 + display 7 segmentos
// =====================================================================
// Matriz en zigzag (serigrafía): fila0 RGB0..3 izq->der, fila1 RGB7..4,
// fila2 RGB8..11, fila3 RGB15..12.
int pxIndex(int x, int y) { return (y % 2 == 0) ? y * 4 + x : y * 4 + (3 - x); }

// Fuente de 4 filas: cada columna es una máscara (bit0 = fila de arriba)
struct Glifo { char c; uint8_t n; uint8_t col[5]; };
const Glifo FUENTE[] = {
  {'R', 3, {0b1111, 0b0101, 0b1011}},
  {'O', 3, {0b1111, 0b1001, 0b1111}},
  {'M', 5, {0b1111, 0b0010, 0b0100, 0b0010, 0b1111}},
  {'E', 3, {0b1111, 0b1011, 0b1001}},
  {'L', 3, {0b1111, 0b1000, 0b1000}},
  {'F', 3, {0b1111, 0b0011, 0b0001}},
  {'I', 3, {0b1001, 0b1111, 0b1001}},
  {'D', 3, {0b1111, 0b1001, 0b0110}},
  {'A', 3, {0b1110, 0b0101, 0b1110}},
  {'S', 3, {0b1011, 0b1011, 0b1101}},
  {'Y', 3, {0b0011, 0b1100, 0b0011}},
  {'+', 3, {0b0100, 0b1110, 0b0100}},
  {' ', 2, {0, 0}},
};
// Letras para el display de 7 segmentos (bits: A=1 B=2 C=4 D=8 E=16 F=32 G=64)
uint8_t seg7(char c) {
  switch (c) {
    case 'R': return 0x50; case 'O': return 0x3F; case 'M': return 0x37;
    case 'E': return 0x79; case 'L': return 0x38; case 'F': return 0x71;
    case 'I': return 0x30; case 'D': return 0x5E; case 'A': return 0x77;
    case 'S': return 0x6D; case 'Y': return 0x6E; case '+': return 0x46;
    default:  return 0x00;
  }
}

const char *NOMBRES[] = {"ROMMEL", "FIDIA"};
bool nombresOn = false;
uint8_t colBuf[96]; int colLen = 0;
int nombreIdx = 0, scrollPos = 0;

void prepararNombre(const char *txt) {
  colLen = 0;
  for (int k = 0; k < 4; k++) colBuf[colLen++] = 0;            // entra desde la derecha
  for (const char *p = txt; *p; p++)
    for (const Glifo &g : FUENTE)
      if (g.c == *p) {
        for (int k = 0; k < g.n; k++) colBuf[colLen++] = g.col[k];
        colBuf[colLen++] = 0;                                   // espacio entre letras
      }
  for (int k = 0; k < 3; k++) colBuf[colLen++] = 0;            // sale por la izquierda
  scrollPos = 0;
}

void mostrar7segTexto(const char *txt, int desde) {
  uint8_t d[4];
  int n = strlen(txt);
  for (int i = 0; i < 4; i++) {
    int k = desde + i - 3;                                      // entra desde la derecha
    d[i] = (k >= 0 && k < n) ? seg7(txt[k]) : 0;
  }
  seg.setSegments(d);
}

void nombresTick() {
  static unsigned long last = 0, last7 = 0;
  static int pos7 = 0;
  static uint16_t hue = 0;
  unsigned long now = millis();
  // Matriz: desplaza una columna cada 170 ms, colores arcoíris que giran
  if (now - last >= 170) {
    last = now;
    hue += 1500;
    pix.setBrightness(35);
    pix.clear();
    for (int x = 0; x < 4; x++) {
      int c = scrollPos + x;
      uint8_t m = (c < colLen) ? colBuf[c] : 0;
      for (int y = 0; y < 4; y++)
        if (m & (1 << y))
          pix.setPixelColor(pxIndex(x, y), pix.gamma32(pix.ColorHSV(hue + x * 9000 + y * 4000)));
    }
    pix.show();
    if (++scrollPos > colLen - 4) {                             // terminó el nombre
      nombreIdx = (nombreIdx + 1) % 2;
      prepararNombre(NOMBRES[nombreIdx]);
      pos7 = 0;
      oledMsg("Nombre en pantalla:", "", String("  >> ") + NOMBRES[nombreIdx] + " <<");
      if (oledOk) {
        oled.setTextSize(2); oled.setCursor(10, 40); oled.print(NOMBRES[nombreIdx]); oled.display();
      }
    }
  }
  // 7 segmentos: mismo nombre, una letra cada 350 ms
  if (now - last7 >= 350) {
    last7 = now;
    const char *t = NOMBRES[nombreIdx];
    mostrar7segTexto(t, pos7);
    pos7 = (pos7 + 1) % (strlen(t) + 4);
  }
}

void toggleNombres() {
  nombresOn = !nombresOn;
  if (nombresOn) {
    monitorOn = false; nombreIdx = 0; prepararNombre(NOMBRES[0]);
    oledMsg("Nombre en pantalla:", "", "  >> ROMMEL <<");
    Serial.println(F("Nombres rotativos: ON (n para salir)"));
  } else {
    pix.clear(); pix.show(); seg.clear();
    Serial.println(F("Nombres rotativos: OFF"));
  }
}

// =====================================================================
//  BOTONES (cruz SW3..SW7 en GPIO34, escalera de resistencias)
// =====================================================================
Preferences prefs;
enum { B_NADA = -1, B_ARRIBA, B_ABAJO, B_IZQ, B_DER, B_CENTRO };
const char *NOMBRE_BTN[5] = {"ARRIBA", "ABAJO", "IZQUIERDA", "DERECHA", "CENTRO"};
int btnVal[5] = {-1, -1, -1, -1, -1};
int btnReposo = -1;
bool btnCalibrado = false, juegoOn = false;

int leerBotonRaw() {               // promedio de 8 lecturas
  long s = 0;
  for (int i = 0; i < 8; i++) s += analogRead(BOTONES_IR);
  return s / 8;
}

int leerBoton() {
  if (!btnCalibrado || irOn) return B_NADA;
  int v = leerBotonRaw();
  int mejor = B_NADA, dist = abs(v - btnReposo);
  for (int i = 0; i < 5; i++)
    if (btnVal[i] >= 0 && abs(v - btnVal[i]) < dist) { dist = abs(v - btnVal[i]); mejor = i; }
  return mejor;
}

void cargarCalibracion() {
  prefs.begin("bmx01", true);
  btnReposo = prefs.getInt("reposo", -1);
  for (int i = 0; i < 5; i++) btnVal[i] = prefs.getInt(("b" + String(i)).c_str(), -1);
  prefs.end();
  btnCalibrado = btnReposo >= 0;
  for (int i = 0; i < 5; i++) if (btnVal[i] < 0) btnCalibrado = false;
}

void calibrarBotones() {
  titulo("c. Calibracion de botones (SW2 en BOTONES)");
  monitorOn = nombresOn = juegoOn = false;
  btnReposo = leerBotonRaw();
  Serial.printf("  Reposo (sin pulsar): %d\n", btnReposo);
  for (int i = 0; i < 5; i++) {
    Serial.printf("  Pulsa y suelta: %s ... ", NOMBRE_BTN[i]);
    oledMsg("Calibrar botones", String("Pulsa: ") + NOMBRE_BTN[i], "(20 s maximo)");
    unsigned long t0 = millis();
    int v = btnReposo;
    while (millis() - t0 < 20000) {                             // esperar pulsación estable
      v = leerBotonRaw();
      if (abs(v - btnReposo) > 200) {
        delay(60);
        int v2 = leerBotonRaw();
        if (abs(v2 - v) < 120) { v = (v + v2) / 2; break; }
      }
      delay(10);
    }
    if (abs(v - btnReposo) <= 200) { Serial.println("sin respuesta"); warn("Boton no detectado"); continue; }
    btnVal[i] = v;
    Serial.printf("valor %d\n", v);
    beep(1500, 60);
    while (abs(leerBotonRaw() - btnReposo) > 200 && millis() - t0 < 25000) delay(10);  // esperar soltar
    delay(200);
  }
  prefs.begin("bmx01", false);
  prefs.putInt("reposo", btnReposo);
  for (int i = 0; i < 5; i++) prefs.putInt(("b" + String(i)).c_str(), btnVal[i]);
  prefs.end();
  cargarCalibracion();
  if (btnCalibrado) ok("Botones calibrados y guardados en memoria"); else warn("Calibracion incompleta");
}

// Juego: mover un punto por la matriz; centro cambia color y suena
int jx = 1, jy = 1, jHue = 0, jPulsos = 0;
void dibujarJuego(int b) {
  pix.setBrightness(40);
  pix.fill(pix.Color(2, 2, 6));
  pix.setPixelColor(pxIndex(jx, jy), pix.ColorHSV(jHue * 8192));
  pix.show();
  seg.showNumberDec(jPulsos);
  if (oledOk) {
    oled.clearDisplay(); oled.setTextSize(1); oled.setCursor(0, 0);
    oled.println(F("== JUEGO BOTONES =="));
    oled.printf("Ultimo: %s\n", b >= 0 ? NOMBRE_BTN[b] : "-");
    oled.printf("Posicion: (%d,%d)\nPulsaciones: %d\n", jx, jy, jPulsos);
    for (int y = 0; y < 4; y++) for (int x = 0; x < 4; x++)  // mini matriz
      (x == jx && y == jy ? oled.fillRect(88 + x * 10, 24 + y * 10, 8, 8, SSD1306_WHITE)
                          : oled.drawRect(88 + x * 10, 24 + y * 10, 8, 8, SSD1306_WHITE));
    oled.display();
  }
}
void juegoTick() {
  static int previo = B_NADA;
  int b = leerBoton();
  if (b != previo && b != B_NADA) {
    jPulsos++;
    switch (b) {
      case B_ARRIBA: jy = (jy + 3) % 4; beep(880, 30); break;
      case B_ABAJO:  jy = (jy + 1) % 4; beep(660, 30); break;
      case B_IZQ:    jx = (jx + 3) % 4; beep(740, 30); break;
      case B_DER:    jx = (jx + 1) % 4; beep(990, 30); break;
      case B_CENTRO: jHue = (jHue + 1) % 8; beep(1320, 60); beep(1760, 60); break;
    }
    Serial.printf("Boton: %s -> (%d,%d)\n", NOMBRE_BTN[b], jx, jy);
    dibujarJuego(b);
  }
  previo = b;
  delay(20);
}
void toggleJuego() {
  if (!btnCalibrado) { Serial.println(F("Primero calibra los botones con 'c'")); return; }
  juegoOn = !juegoOn;
  if (juegoOn) { monitorOn = nombresOn = false; jPulsos = 0; dibujarJuego(B_NADA); Serial.println(F("Juego de botones: ON (j para salir)")); }
  else { pix.clear(); pix.show(); seg.clear(); Serial.println(F("Juego de botones: OFF")); }
}

// Prueba rápida de bocina + ultrasonido: "sensor de estacionamiento"
void pruebaEstacionamiento() {
  titulo("e. Sensor de estacionamiento (ultrasonido + bocina + 7seg), 15 s");
  oledMsg("Acerca la mano", "al sensor HC-SR04");
  unsigned long t0 = millis(), ultimoBeep = 0;
  float minimo = 9999; int lecturas = 0;
  while (millis() - t0 < 15000) {
    float d = leerDistanciaCm(usEcho);
    if (d > 0) {
      lecturas++; minimo = min(minimo, d);
      seg.showNumberDec((int)d);
      int n = constrain((int)(d / 10), 0, 15);
      pix.setBrightness(40);
      for (int i = 0; i < N_PIXELES; i++)
        pix.setPixelColor(i, i < 16 - n ? (d < 15 ? pix.Color(255, 0, 0) : d < 40 ? pix.Color(255, 160, 0) : pix.Color(0, 255, 0)) : 0);
      pix.show();
      int periodo = d < 8 ? 0 : constrain((int)(d * 8), 60, 1000);
      if (d < 60 && (periodo == 0 || millis() - ultimoBeep > (unsigned)periodo)) { beep(1800, 40); ultimoBeep = millis(); }
    } else seg.showNumberDec(-1);
    delay(60);
  }
  pix.clear(); pix.show(); seg.clear();
  Serial.printf("  Lecturas validas: %d  distancia minima: %.1f cm\n", lecturas, minimo);
  if (lecturas) ok("Ultrasonido + bocina funcionando"); else warn("Sin lecturas del ultrasonido");
}

// =====================================================================
//  BÚSQUEDA DE FALLAS: bocina y motores (pasos lentos, anunciados en OLED)
// =====================================================================
void anunciar(const String &a, const String &b, const String &c = "") {
  Serial.printf("  >> %s | %s %s\n", a.c_str(), b.c_str(), c.c_str());
  oledMsg(a, b, c);
}

void fallaBocina() {
  titulo("x. Busqueda de falla: bocina");
  const int pines[] = {12, 32};
  for (int p : pines) {
    ledcDetach(p);
    pinMode(p, OUTPUT);
    // 1) tono (bocina pasiva)
    ledcAttach(p, 2000, 8);
    anunciar("GPIO" + String(p) + ": TONO", "2 kHz, 2 segundos");
    for (int f = 1000; f <= 3000; f += 50) { ledcWriteTone(p, f); delay(50); }
    ledcWriteTone(p, 0); ledcDetach(p);
    delay(400);
    // 2) nivel alto fijo (bocina activa, transistor NPN)
    pinMode(p, OUTPUT);
    anunciar("GPIO" + String(p) + ": ALTO", "nivel 1 fijo 2 s");
    digitalWrite(p, HIGH); delay(2000);
    // 3) nivel bajo fijo (transistor PNP)
    anunciar("GPIO" + String(p) + ": BAJO", "nivel 0 fijo 2 s");
    digitalWrite(p, LOW); delay(2000);
    // 4) onda cuadrada manual (sin LEDC)
    anunciar("GPIO" + String(p) + ": ONDA MANUAL", "1 kHz por software");
    for (int i = 0; i < 1500; i++) { digitalWrite(p, i & 1); delayMicroseconds(500); }
    digitalWrite(p, LOW);
    delay(600);
  }
  ledcAttach(BOCINA, 2000, 8);
  anunciar("Fin prueba bocina", "Dime en que paso", "sono algo");
}

void fallaMotores() {
  titulo("y. Busqueda de falla: motores");
  // Paso a paso: una bobina a la vez, 2 s cada una (LED1..LED4 deben encender)
  for (int i = 0; i < 4; i++) pinMode(STEP_PINS[i], OUTPUT);
  for (int i = 0; i < 4; i++) {
    stepperOff();
    digitalWrite(STEP_PINS[i], HIGH);
    anunciar("Paso a paso", "Bobina " + String(i + 1) + " GPIO" + String(STEP_PINS[i]), "LED" + String(i + 1) + " debe encender");
    delay(2000);
  }
  stepperOff();
  anunciar("Paso a paso", "Giro lento 5 s", "");
  unsigned long t0 = millis(); int s = 0;
  while (millis() - t0 < 5000) { stepperPaso(s++); delay(4); }
  stepperOff();
  // Motor DC: a máxima potencia, 3 s por sentido
  ledcDetach(MDC_EN); pinMode(MDC_EN, OUTPUT);
  pinMode(MDC_IN1, OUTPUT); pinMode(MDC_IN2, OUTPUT);
  digitalWrite(MDC_EN, HIGH);
  anunciar("Motor DC", "ADELANTE 100%", "EN33=1 IN25=1 IN26=0");
  digitalWrite(MDC_IN1, HIGH); digitalWrite(MDC_IN2, LOW); delay(3000);
  digitalWrite(MDC_IN1, LOW); delay(500);
  anunciar("Motor DC", "ATRAS 100%", "EN33=1 IN25=0 IN26=1");
  digitalWrite(MDC_IN2, HIGH); delay(3000);
  digitalWrite(MDC_IN2, LOW); digitalWrite(MDC_EN, LOW);
  // Servo
  for (int a : {0, 180, 0, 180, 90}) {
    anunciar("Servo GPIO32", String(a) + " grados");
    servoIr(a, 1000);
  }
  anunciar("Fin prueba motores", "Dime que viste", "y que se movio");
}

// Servo por software: pulsos exactos a 50 Hz sin usar LEDC ni temporizadores
void servoPulsos(int us, int ms) {
  unsigned long t0 = millis();
  while (millis() - t0 < (unsigned long)ms) {
    digitalWrite(SERVO_PIN, HIGH); delayMicroseconds(us);
    digitalWrite(SERVO_PIN, LOW);  delayMicroseconds(20000 - us);
  }
}

void fallaServo() {
  titulo("z. Busqueda de falla: servo (GPIO32)");
  ledcDetach(SERVO_PIN);
  pinMode(SERVO_PIN, OUTPUT); digitalWrite(SERVO_PIN, LOW);
  anunciar("Servo: SIN SENAL", "3 s. Debe estar", "quieto y callado");
  delay(3000);
  // A) Pulsos por software (descarta conflictos de temporizador)
  const int pos[] = {1500, 600, 2400, 600, 2400, 1500};
  const char *txt[] = {"centro 90", "0 grados", "180 grados", "0 grados", "180 grados", "centro 90"};
  for (int i = 0; i < 6; i++) {
    anunciar("Servo SOFTWARE", String(pos[i]) + " us", txt[i]);
    servoPulsos(pos[i], 1500);
  }
  digitalWrite(SERVO_PIN, LOW);
  delay(800);
  // B) LEDC dedicado a 50 Hz, 14 bits
  ledcAttach(SERVO_PIN, 50, 14);
  for (int i = 0; i < 6; i++) {
    anunciar("Servo LEDC 50Hz", String(pos[i]) + " us", txt[i]);
    ledcWrite(SERVO_PIN, (uint32_t)pos[i] * 16384 / 20000);
    delay(1500);
  }
  ledcWrite(SERVO_PIN, 0); ledcDetach(SERVO_PIN);
  pinMode(SERVO_PIN, OUTPUT); digitalWrite(SERVO_PIN, LOW);
  ledcAttach(BOCINA, 2000, 8);
  anunciar("Fin prueba servo", "Se movio en", "SOFTWARE / LEDC?");
}

// Herramientas genéricas: T<pin> = tono en un pin, S<pin> = servo por software en un pin
bool pinSeguro(int p) {
  return p >= 0 && p <= 39 && !(p >= 6 && p <= 11) && p != 1 && p != 3 && !(p >= 34 && p <= 39);
}
void tonoEnPin(int p) {
  if (!pinSeguro(p)) { Serial.printf("Pin %d no permitido\n", p); return; }
  ledcDetach(p); pinMode(p, OUTPUT);
  anunciar("TONO en GPIO" + String(p), "barrido 1-3 kHz", "2 segundos");
  for (int i = 0; i < 2; i++)
    for (int f = 1000; f <= 3000; f += 100) {
      unsigned long t0 = micros(); int medio = 500000 / f;
      while (micros() - t0 < 50000) { digitalWrite(p, HIGH); delayMicroseconds(medio); digitalWrite(p, LOW); delayMicroseconds(medio); }
    }
  digitalWrite(p, LOW);
  if (p == BOCINA) ledcAttach(BOCINA, 2000, 8);
  Serial.printf("  Tono en GPIO%d terminado\n", p);
}
void servoEnPin(int p) {
  if (!pinSeguro(p)) { Serial.printf("Pin %d no permitido\n", p); return; }
  ledcDetach(p); pinMode(p, OUTPUT);
  const int pos[] = {1500, 600, 2400, 600, 2400, 1500};
  for (int us : pos) {
    anunciar("SERVO en GPIO" + String(p), String(us) + " us");
    unsigned long t0 = millis();
    while (millis() - t0 < 1200) { digitalWrite(p, HIGH); delayMicroseconds(us); digitalWrite(p, LOW); delayMicroseconds(20000 - us); }
  }
  digitalWrite(p, LOW);
  if (p == BOCINA) ledcAttach(BOCINA, 2000, 8);
  Serial.printf("  Servo en GPIO%d terminado\n", p);
}

// ---------------- Monitor en vivo ----------------
void monitorTick() {
  static unsigned long last = 0;
  if (millis() - last < 250) return;
  last = millis();
  bool v; String fuente;
  float t = leerTemperatura(v, fuente);
  int luz = analogRead(LUZ), pot = analogRead(POT), incl = digitalRead(INCLINACION);
  int bot = irOn ? -1 : analogRead(BOTONES_IR);
  float d = leerDistanciaCm(usEcho);

  // Matriz: color según potenciómetro, cantidad de LEDs según luz
  int encendidos = map(luz, 0, 4095, 0, N_PIXELES);
  pix.setBrightness(30);
  for (int i = 0; i < N_PIXELES; i++)
    pix.setPixelColor(i, i < encendidos ? pix.ColorHSV(pot * 16) : 0);
  pix.show();
  // 7 segmentos: distancia en cm
  if (d > 0) seg.showNumberDec((int)d); else seg.showNumberDec(-1);

  if (oledOk) {
    oled.clearDisplay(); oled.setTextSize(1); oled.setCursor(0, 0);
    oled.println(F("== MONITOR EN VIVO =="));
    oled.printf("Temp : %.1f C\n", t);
    oled.printf("Luz  : %d\n", luz);
    oled.printf("Pot  : %d%%\n", pot * 100 / 4095);
    oled.printf("Dist : %.1f cm\n", d);
    oled.printf("Incl : %s\n", incl ? "1" : "0");
    if (bot < 0) oled.print("IR activo");
    else { int b = leerBoton(); oled.printf("Boton: %d %s", bot, b >= 0 ? NOMBRE_BTN[b] : ""); }
    oled.drawRect(0, 60, 128, 4, SSD1306_WHITE);
    oled.fillRect(0, 60, pot * 128 / 4095, 4, SSD1306_WHITE);
    oled.display();
  }
  Serial.printf("T=%.1fC luz=%d pot=%d incl=%d dist=%.1fcm gpio34=%d\n", t, luz, pot, incl, d, bot);
}

// ---------------- Página web (modo punto de acceso) ----------------
const char PAGINA[] PROGMEM = R"HTML(<!doctype html><html><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1"><title>BMX-01</title>
<style>body{font-family:sans-serif;background:#0b2a5b;color:#fff;margin:16px}h1{font-size:20px}
.g{display:grid;grid-template-columns:1fr 1fr;gap:8px}.c{background:#163d7a;border-radius:8px;padding:10px}
.v{font-size:24px;font-weight:bold}button{padding:12px;margin:4px;border:0;border-radius:8px;font-size:15px}</style></head>
<body><h1>Placa BMX-01 &middot; Semilleros Cient&iacute;ficos</h1><div class="g">
<div class="c">Temperatura<div class="v" id="t">-</div></div><div class="c">Luz<div class="v" id="l">-</div></div>
<div class="c">Potenci&oacute;metro<div class="v" id="p">-</div></div><div class="c">Distancia<div class="v" id="d">-</div></div>
<div class="c">Inclinaci&oacute;n<div class="v" id="i">-</div></div><div class="c">Rel&eacute;<div class="v" id="r">-</div></div></div>
<p><button onclick="c('rele')">Rel&eacute;</button><button onclick="c('azul')" style="background:#39f">LED azul</button>
<button onclick="c('verde')" style="background:#3c3">LED verde</button><button onclick="c('beep')">Bocina</button>
<button onclick="c('rgb')">Matriz RGB</button><button onclick="c('servo')">Servo</button></p>
<script>function c(x){fetch('/cmd?x='+x)}
setInterval(()=>fetch('/data').then(r=>r.json()).then(j=>{t.textContent=j.t+' °C';l.textContent=j.l;
p.textContent=j.p+' %';d.textContent=j.d+' cm';i.textContent=j.i;r.textContent=j.r?'ON':'OFF'}),700)</script></body></html>)HTML";

bool releEstado = false, rgbEstado = false;

void apIniciar() {
  titulo("p. Punto de acceso WiFi + pagina web");
  WiFi.mode(WIFI_AP);
  WiFi.softAP("BMX01-Diagnostico", "semilleros");
  apOn = true;
  web.on("/", [] { web.send_P(200, "text/html", PAGINA); });
  web.on("/data", [] {
    bool v; String f; float t = leerTemperatura(v, f);
    char b[160];
    snprintf(b, sizeof b, "{\"t\":\"%.1f\",\"l\":%d,\"p\":%d,\"d\":\"%.1f\",\"i\":%d,\"r\":%d}",
             t, analogRead(LUZ), analogRead(POT) * 100 / 4095, leerDistanciaCm(usEcho),
             digitalRead(INCLINACION), releEstado);
    web.send(200, "application/json", b);
  });
  web.on("/cmd", [] {
    String x = web.arg("x");
    if (x == "rele") { releEstado = !releEstado; pinMode(RELE, OUTPUT); digitalWrite(RELE, releEstado); }
    else if (x == "azul") { pinMode(LED_AZUL, OUTPUT); digitalWrite(LED_AZUL, !digitalRead(LED_AZUL)); }
    else if (x == "verde") { pinMode(LED_VERDE, OUTPUT); digitalWrite(LED_VERDE, !digitalRead(LED_VERDE)); }
    else if (x == "beep") beep(1000, 150);
    else if (x == "rgb") { rgbEstado = !rgbEstado; pix.setBrightness(40); pix.fill(rgbEstado ? pix.Color(0, 80, 255) : 0); pix.show(); }
    else if (x == "servo") { static int a = 0; a = (a == 0) ? 180 : 0; servoIr(a, 600); }
    web.send(200, "text/plain", "ok");
  });
  web.begin();
  Serial.println(F("  Red WiFi: BMX01-Diagnostico   clave: semilleros"));
  Serial.printf("  Abrir en el navegador: http://%s\n", WiFi.softAPIP().toString().c_str());
  oledMsg("WiFi: BMX01-Diagnostico", "Clave: semilleros", "http://" + WiFi.softAPIP().toString());
  ok("Servidor web activo");
}

// ---------------- Menú ----------------
void ayuda() {
  Serial.println(F("\n========== MENU DIAGNOSTICO BMX-01 =========="));
  Serial.println(F(" a: diagnostico automatico   h: ayuda"));
  Serial.println(F(" 0: info chip  i: I2C   1: LEDs   2: bocina  3: rele"));
  Serial.println(F(" 4: matriz RGB 5: OLED  6: 7seg   7: sensores"));
  Serial.println(F(" 8: motor DC   9: paso a paso     s: servo"));
  Serial.println(F(" w: WiFi scan  b: BLE scan        r: receptor IR on/off"));
  Serial.println(F(" m: monitor en vivo on/off        p: punto de acceso web"));
  Serial.println(F(" n: nombres ROMMEL/FIDIA on/off   e: sensor de estacionamiento"));
  Serial.println(F(" c: calibrar botones              j: juego con botones on/off"));
  Serial.println(F(" x: busqueda falla bocina         y: busqueda falla motores"));
  Serial.println(F(" z: busqueda falla servo         T<pin>: tono en pin   S<pin>: servo en pin"));
  Serial.println(F("============================================="));
}

void diagnosticoCompleto() {
  okCount = warnCount = 0;
  unsigned long t0 = millis();
  pruebaInfoChip(); pruebaI2C(); pruebaOLED(); pruebaLEDs(); pruebaBocina(); pruebaRele();
  pruebaMatriz(); prueba7Seg(); pruebaSensores(); pruebaMotorDC(); pruebaStepper(); pruebaServo();
  pruebaWiFi(); pruebaBLE();
  titulo("RESUMEN");
  Serial.printf("  %d pruebas OK, %d avisos, %lu s\n", okCount, warnCount, (millis() - t0) / 1000);
  oledMsg("Diagnostico listo", String(okCount) + " OK  " + String(warnCount) + " avisos", "Escribe h = menu");
  beep(1319, 100); beep(1760, 200);
}

void toggleIR() {
  irOn = !irOn;
  if (irOn) { IrReceiver.begin(BOTONES_IR, DISABLE_LED_FEEDBACK); Serial.println(F("IR activo: apunta un control remoto (SW2 en RECEPTOR)")); }
  else { IrReceiver.end(); Serial.println(F("IR desactivado")); }
}

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println(F("\n\n*** BMX-01 · Programa de diagnostico v1.3 ***"));

  pinMode(US_TRIG, OUTPUT);
  pinMode(2, INPUT);
  pinMode(INCLINACION, INPUT);
  pinMode(LED_AZUL, OUTPUT); pinMode(LED_VERDE, OUTPUT);
  pinMode(MDC_IN1, OUTPUT); pinMode(MDC_IN2, OUTPUT);
  for (int i = 0; i < 4; i++) pinMode(STEP_PINS[i], OUTPUT);
  stepperOff();
  ledcAttach(BOCINA, 2000, 8);
  analogReadResolution(12);

  Wire.begin(OLED_SDA, OLED_SCL);
  for (uint8_t a : {0x3C, 0x3D}) {
    Wire.beginTransmission(a);
    if (Wire.endTransmission() == 0) { oledAddr = a; break; }
  }
  if (oledAddr && oled.begin(SSD1306_SWITCHCAPVCC, oledAddr)) oledOk = true;
  oledMsg("Iniciando...", "Semilleros", "Cientificos");

  pix.begin(); pix.clear(); pix.show();
  seg.setBrightness(5); seg.clear();

  ds.begin();
  dsOk = ds.getDeviceCount() > 0;
  Serial.printf("OLED: %s | DS18B20: %s\n", oledOk ? "si" : "no", dsOk ? "si" : "no");

  cargarCalibracion();
  Serial.printf("Botones calibrados: %s\n", btnCalibrado ? "si" : "no (usa c)");
  diagnosticoCompleto();
  ayuda();
  toggleNombres();   // al terminar, mostrar los nombres
}

void loop() {
  if (Serial.available()) {
    char c = Serial.read();
    switch (c) {
      case 'a': diagnosticoCompleto(); break;
      case 'h': case '?': ayuda(); break;
      case '0': pruebaInfoChip(); break;
      case 'i': pruebaI2C(); break;
      case '1': pruebaLEDs(); break;
      case '2': pruebaBocina(); break;
      case '3': pruebaRele(); break;
      case '4': pruebaMatriz(); break;
      case '5': pruebaOLED(); break;
      case '6': prueba7Seg(); break;
      case '7': pruebaSensores(); break;
      case '8': pruebaMotorDC(); break;
      case '9': pruebaStepper(); break;
      case 's': pruebaServo(); break;
      case 'w': pruebaWiFi(); break;
      case 'b': pruebaBLE(); break;
      case 'r': toggleIR(); break;
      case 'p': apIniciar(); break;
      case 'n': toggleNombres(); break;
      case 'x': nombresOn = monitorOn = juegoOn = false; fallaBocina(); break;
      case 'y': nombresOn = monitorOn = juegoOn = false; fallaMotores(); break;
      case 'z': nombresOn = monitorOn = juegoOn = false; fallaServo(); break;
      case 'T': nombresOn = monitorOn = juegoOn = false; tonoEnPin(Serial.parseInt()); break;
      case 'S': nombresOn = monitorOn = juegoOn = false; servoEnPin(Serial.parseInt()); break;
      case 'c': calibrarBotones(); break;
      case 'j': toggleJuego(); break;
      case 'e': nombresOn = monitorOn = juegoOn = false; pruebaEstacionamiento(); break;
      case 'm':
        monitorOn = !monitorOn; nombresOn = juegoOn = false;
        Serial.println(monitorOn ? F("Monitor en vivo: ON (m para salir)") : F("Monitor en vivo: OFF"));
        if (!monitorOn) { pix.clear(); pix.show(); seg.clear(); }
        break;
    }
  }
  if (monitorOn) monitorTick();
  if (nombresOn) nombresTick();
  if (juegoOn) juegoTick();
  if (apOn) web.handleClient();
  if (irOn && IrReceiver.decode()) {
    Serial.printf("IR: protocolo=%s direccion=0x%04X comando=0x%02X\n",
                  getProtocolString(IrReceiver.decodedIRData.protocol),
                  IrReceiver.decodedIRData.address, IrReceiver.decodedIRData.command);
    beep(2000, 40);
    IrReceiver.resume();
  }
}
