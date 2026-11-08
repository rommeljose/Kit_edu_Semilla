#!/usr/bin/env python3
"""Uso: serie.py [--reset] [--segundos N] [comandos...]
Abre /dev/ttyUSB0 a 115200, opcionalmente reinicia la placa, envía cada comando
(una letra del menú) y muestra lo que responde la placa."""
import serial, sys, time, argparse
ap = argparse.ArgumentParser()
ap.add_argument('--puerto', default='/dev/ttyUSB0')
ap.add_argument('--reset', action='store_true')
ap.add_argument('--segundos', type=float, default=5)
ap.add_argument('cmds', nargs='*')
a = ap.parse_args()
s = serial.Serial()
s.port, s.baudrate, s.timeout = a.puerto, 115200, 0.1
s.dtr = False; s.rts = False   # no mantener la placa en reset/boot al abrir
s.open()
if a.reset:
    s.rts = True; time.sleep(0.1); s.rts = False
def leer(t):
    fin = time.time() + t
    while time.time() < fin:
        d = s.read(4096)
        if d: sys.stdout.write(d.decode('utf-8', 'replace')); sys.stdout.flush()
leer(a.segundos)
for c in a.cmds:
    s.write(c.encode()); leer(a.segundos)
