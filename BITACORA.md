# Bitácora – Kit educativo BMX-01 (Semilleros Científicos, Mincyt)

## 2026-11-08 · Sesión 1: Reconocimiento del kit

### Identificación (a partir de las fotos en Drive)
- Placa **BMX-01** (rev. 25.19), fabricada en Venezuela (Sistemas Tecnológicos / Mincyt / Gran Misión Ciencia, Tecnología e Innovación).
- Contenido de la caja: placa, motor DC, motor paso a paso, cable USB-C y manual de uso.
- Microcontrolador **ESP32-WROOM-32D**. USB-serial **CH340C**, regulador **AMS1117-3.3**, botones EN/BOOT.
- Otros CI: **L293** (motor DC), **ULN2003** (paso a paso), **TM1637** (display de 7 segmentos), relé **SRD-03VDC-SL-C**.

### Mapa de pines (leyenda serigrafiada al reverso)
| Periférico | GPIO |
|---|---|
| LED azul / verde / rojo / amarillo | 19 / 23 / 1 / 3 |
| Bocina (switch SW1 ↔ pin libre) | 12 |
| Relé | 0 |
| Matriz RGB 4×4 | 13 |
| OLED I2C | SCL 22, SDA 21 |
| Display 7 seg (TM1637) | CLK 27, DIO 14 |
| Sensor de temperatura | 4 |
| Sensor de inclinación | 39 |
| Sensor de luz (LDR) | 35 |
| Potenciómetro | 36 |
| Ultrasonido HC-SR04 | Trig 15, Echo 2 (placa) / 12 (manual) ⚠ |
| Botones (switch SW2 ↔ receptor IR) | 34 |
| Paso a paso | 16, 17, 5, 18 |
| Motor DC | EN 33, IN1 25, IN2 26 |
| Servo | 32 |

**Observaciones:**
- Los LEDs rojo y amarillo usan TX/RX (GPIO1 y GPIO3), por lo que hay conflicto con el puerto serie.
- El relé está en GPIO0, que es un pin de arranque (strapping).
- Hay una discrepancia en el pin Echo del ultrasonido: la placa dice 2 y el manual dice 12.
- El cabezal H4 está serigrafiado como "IO26", pero la leyenda dice "pin libre GPIO12".

### Conexión a la PC
- En Windows: CH340 en COM7, BUSID 1-11, conectado a WSL con `usbipd attach --wsl --busid 1-11`. En Linux aparece como `/dev/ttyUSB0`.
- `esptool chip-id` reporta: **ESP32-D0WD rev v1.1**, 2 núcleos a 240 MHz, cristal de 40 MHz, flash de **4 MB** (fabricante 0x46, dispositivo 0x4016). MAC: se omite por privacidad.
- Firmware de fábrica: bootloader ESP-IDF v4.0.1, particiones app0/app1 (OTA) y spiffs.
- **Respaldo del firmware original:** `respaldo/firmware_original_4MB.bin`.
  - Las lecturas completas a 921600, 460800 y 115200 baudios fallaron con "Corrupt data": usbip pierde bytes en transferencias largas.
  - Solución: leer en bloques de 64 KB y luego de 16 KB, con reintentos.
  - Para restaurar: `esptool --port /dev/ttyUSB0 write-flash 0 respaldo/firmware_original_4MB.bin`

### Entorno de desarrollo
- arduino-cli 1.5.1, núcleo esp32:esp32 3.3.12.
- Librerías instaladas: Adafruit SSD1306, Adafruit GFX, Adafruit NeoPixel, TM1637 (Avishay), IRremote 4.7.1, ESP32Servo 3.2.1, OneWire, DallasTemperature.
- Compilación con `PartitionScheme=huge_app` (3 MB para la aplicación, sin OTA), porque WiFi y BLE juntos no caben en la partición por defecto.

### Programa de diagnóstico `diagnostico_bmx01/`
- Al arrancar ejecuta una batería automática de pruebas y luego queda un menú por monitor serie (115200 baudios).
- Incluye monitor en vivo de sensores (OLED, 7 segmentos y matriz RGB) y un punto de acceso WiFi con página web de control.

### Respaldo verificado
- `respaldo/firmware_original_4MB.bin`, SHA-256 `2e99f3ea…b06667c2`. Coincide con el chip (`esptool verify-flash`: "digest matched").

### Primera carga del diagnóstico (v1.0 + nombres, botones y estacionamiento)
- Carga a 115200 baudios en unos 106 s (a mayor velocidad usbip corrompe datos).
- **Problema con el puerto serie:** cada vez que se abre `/dev/ttyUSB0`, la placa se reinicia, y si RTS queda activo la mantiene en reset.
  - Solución: `herramientas/puente.py`, que abre el puerto una sola vez, registra todo en `herramientas/serie.log` y recibe comandos por `herramientas/cmd.fifo`.
  - Ejemplo: `echo -n e > herramientas/cmd.fifo`
- **Resultado: 15 pruebas OK, 0 avisos, 36 s.**
  - Sensor de temperatura confirmado: **DS18B20 (1-Wire) en GPIO4**, 29,0 °C.
  - **El Echo del ultrasonido está en GPIO2.** La placa tiene razón y el manual se equivoca (dice GPIO12). Midió 37,0 cm.
  - LDR: 2938/4095. Potenciómetro: 88 %. Inclinación: 1. GPIO34 en reposo: 4095.
  - OLED SSD1306 en I2C 0x3C. WiFi: 12 redes detectadas. BLE: 1 dispositivo.
  - Por puerto serie la MAC aparece como 00:00:… porque se consulta antes de encender el WiFi (detalle menor).
- Pendiente de confirmación visual del usuario: LEDs, bocina, relé, matriz, display de 7 segmentos y nombres.
- Pendiente: calibrar botones (`c`), probar el receptor IR (`r`) y conectar los motores y el servo.

### Búsqueda de fallas: motores, servo y bocina
- **Motores (resuelto por el usuario):** la bornera **P2** es la entrada de alimentación de los motores y viene sin conectar de fábrica. Hay que llevar **5 V y GND desde el cabezal H3 hasta P2**. Con eso funcionan el motor DC y el paso a paso.
- **Servo y bocina:** con la señal de servo en GPIO32 (según la leyenda), el servo no se movía y la **bocina** emitía pitidos (el usuario lo comprobó al tacto y de oído).
  - Hipótesis: la leyenda tiene intercambiados la bocina y el servo (bocina en GPIO32, servo en GPIO12).
  - Se agregaron los comandos `T<pin>` (tono en cualquier pin) y `S<pin>` (servo por software en cualquier pin) para comprobarlo.
- **Nota:** los comandos enviados durante el diagnóstico de arranque se pierden, porque la prueba de LEDs reinicia el puerto serie y vacía el búfer de entrada.

### Resultado: pines reales de la bocina y el servo (v1.1)
- Se probó la señal de servo en 16 pines (12, 26, 33, 25, 2, 15, 4, 13, 14, 27, 5, 16, 17, 18, 19 y 23) y no se movió en ninguno.
- El **primer servo estaba dañado**. Con un servo nuevo en **GPIO32** sí se mueve, así que la leyenda es correcta para el servo.
- `T32` hace sonar la bocina, así que **la bocina también está en GPIO32** (la leyenda dice GPIO12, y eso es un error). Bocina y servo comparten el pin: la bocina zumba cuando se mueve el servo.
- Cambios en la versión 1.1:
  - `BOCINA = 32`.
  - Las funciones `servoTomar()` y `servoSoltar()` le quitan el pin a la bocina y se lo devuelven.
  - `fallaBocina` ahora prueba los pines 32 y 12.
- Verificado tras cargarla: arranque con 15 pruebas OK. Los comandos `2` (melodía) y `s` (barrido del servo) se ejecutan.
- **Pendiente:** averiguar qué conecta en realidad el switch SW1 con GPIO12 y el cabezal H4 (que está serigrafiado "IO26").

### Errores de la documentación confirmados
| Elemento | Leyenda o manual | Real |
|---|---|---|
| Echo del ultrasonido | GPIO12 (manual) | **GPIO2** (la serigrafía de la placa es correcta) |
| Bocina | GPIO12 | **GPIO12 es correcto**, pero solo con SW1 en «BOCINA» (de fábrica venía en «LIBRE») |
| Alimentación de motores | (sin indicación) | Puente de **H3 (5 V y GND) a P2** |
- Prueba `x` de la bocina (GPIO32 y GPIO12; tono, nivel alto, nivel bajo y onda por software): el usuario confirma que **la bocina y el servo funcionan bien ("ambos ok")**.
  - Falta registrar en qué paso sonó más fuerte y en qué posición estaba SW1.

### Corrección final de la bocina (v1.2)
- La conclusión de la v1.1 estaba equivocada.
- En la prueba `x`, la bocina suena **fuerte y clara en GPIO12** (pasos 5 a 8) con **SW1 en la posición "BOCINA"**. En la otra posición ("LIBRE") GPIO12 va al cabezal H4.
- Por GPIO32 solo se oía un zumbido bajo: es acoplamiento de la señal del servo. Eso explica los "pitidos" que se oían al probar el servo.
- La falla original de la bocina se debía a que **SW1 estaba en "LIBRE"**.
- La v1.2 vuelve a `BOCINA = 12`, y el servo ya no comparte pin con la bocina. Arranque verificado: 15 pruebas OK. Se tocó la melodía con `2`.

### v1.3: melodía y servo estables (confirmado por el usuario: "¡perfecto!")
- **Falla 1:** en la v1.2 la melodía (`2`) no sonaba después del arranque.
  - Causa: la prueba de sensores hacía `pinMode(12, INPUT)` para probar el Echo en GPIO12. Eso desconectaba el LEDC de la bocina.
  - Solución: se eliminó esa prueba, porque el Echo ya está confirmado en GPIO2.
- **Falla 2:** el servo daba un solo salto.
  - Causa: la librería ESP32Servo comparte los temporizadores LEDC con la bocina (2 kHz) y con el motor DC (1 kHz), y pierde los 50 Hz.
  - Solución: se eliminó ESP32Servo. El servo ahora se maneja con pulsos por software a 50 Hz (`servoIr(grados, ms)`, de 500 a 2400 µs).
- Verificado: arranque con 15 pruebas OK, melodía con `2` y barrido del servo con `s` (0°, 90°, 180°, 90°).
- **Lección:** en el ESP32 conviene no mezclar librerías que usan LEDC con frecuencias distintas. Para el servo bastan pulsos por software o un LEDC propio a 50 Hz.

## 2026-11-08 · Publicación: repositorio, guía y página web
- Repositorio público **github.com/rommeljose/Kit_edu_Semilla**, con página en GitHub Pages (carpeta `docs/`).
- **Guía de usuario:** `GUIA_DE_USUARIO.md`, que se convierte a PDF (`docs/guia/Guia_de_usuario_BMX-01.pdf`, 18 páginas) con `pandoc` y WeasyPrint usando `herramientas/pdf/guia.css`.
  - Para regenerarla: `pandoc GUIA_DE_USUARIO.md -s --toc -o /tmp/g.html && weasyprint --base-url "$PWD/" -s herramientas/pdf/guia.css /tmp/g.html docs/guia/Guia_de_usuario_BMX-01.pdf`
- **Diagramas SVG:** `docs/img/mapa_placa.svg` (mapa con los pines verificados) y `docs/img/alimentacion.svg` (puente de H3 a P2 y switches).
- **Fotos:** sacadas del Drive del usuario, reorientadas, reducidas a 1600 px y **sin metadatos EXIF**, que incluían coordenadas GPS.
- **Siete ejemplos** en `ejemplos/`, cada uno compilado sin errores con esp32 3.3.12.
- **No se publica:** el respaldo del firmware de fábrica (`respaldo/`), los registros serie (que contienen SSID y equipos Bluetooth de vecinos) ni la carpeta `build/`.
- Créditos: "Desarrollo y ejemplos: Licenciado Físico Rommel Contreras".
- **Privacidad:** la foto `caja_contenido.jpg` se recortó para quitar la etiqueta con el código de barras y su numeración, a pedido del usuario. En `placa_reverso.jpg` se pixeló el código QR, que contiene la MAC de la placa, y la MAC se quitó de esta bitácora. Con autorización del usuario se reescribió el historial (rama huérfana, un solo commit, `push --force`) para que la foto original no quede en GitHub.

### Corrección de legibilidad del PDF
- Los bloques de código salían con letra clara sobre fondo blanco (el resaltado de pandoc anulaba el fondo oscuro). Ahora: fondo #f2f5fa, texto #10233f, sin resaltado (`--no-highlight`).
- Se oscurecieron las notas (blockquote), los pies de foto y el pie de página. Revisadas las 18 páginas.

### Repositorio recreado (historial purgado)
- Con permiso `delete_repo` se borró y recreó `rommeljose/Kit_edu_Semilla` (público, misma descripción, etiquetas y GitHub Pages desde `/docs`).
- Un solo commit publicado; los SHA antiguos (18cce69, 19932d2, 05fcc72, 83e3829, 00bed7a, 286b950) ya no existen en GitHub.
