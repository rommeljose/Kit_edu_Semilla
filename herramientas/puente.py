#!/usr/bin/env python3
"""Puente serie persistente: abre /dev/ttyUSB0 una sola vez (sin reiniciar la placa
cada vez), guarda todo lo recibido en herramientas/serie.log y envía a la placa lo que
se escriba en la tubería herramientas/cmd.fifo.  Ej:  echo -n e > herramientas/cmd.fifo"""
import serial, os, sys, time, select, termios
base = os.path.dirname(os.path.abspath(__file__))
fifo, log = os.path.join(base, 'cmd.fifo'), os.path.join(base, 'serie.log')
if not os.path.exists(fifo): os.mkfifo(fifo)
s = serial.Serial(); s.port, s.baudrate, s.timeout = '/dev/ttyUSB0', 115200, 0
s.dtr = False; s.rts = False; s.open()
if '--reset' in sys.argv:
    s.rts = True; time.sleep(0.1); s.rts = False
fd = os.open(fifo, os.O_RDWR | os.O_NONBLOCK)
with open(log, 'ab', buffering=0) as L:
    L.write(f'\n===== puente iniciado {time.ctime()} =====\n'.encode())
    while True:
        r, _, _ = select.select([s.fileno(), fd], [], [], 1)
        if s.fileno() in r:
            d = s.read(4096)
            if d: L.write(d)
        if fd in r:
            c = os.read(fd, 256)
            if c: s.write(c); L.write(b'\n<<< enviado: ' + c + b' >>>\n')
