#!/usr/bin/env python3
"""Ergänzt die Tastentabelle in PureDarwins Xorg-Eingabetreiber (puredarwininput_drv.so).

  inputfix.py <puredarwininput_drv.so>      patcht die Datei an Ort und Stelle (ein zweiter Aufruf
                                           ändert nichts)

puredarwininput übersetzt HID-Tastencodes (Seite 7) mit der Tabelle usbToEvdev in Linux-Tastencodes,
Xorg nimmt davon +8 als Keycode. In PureDarwins Tabelle fehlen Druck, Rollen, Pause, Einfg, Pos1,
Bild hoch/runter, Entf, Ende, das ganze Ziffernfeld, die Taste <>| deutscher Tastaturen (ISO) und
die Menütaste: Sie stehen auf 0, und puredarwininput lässt solche Tasten fallen. Das betrifft
USB-Tastaturen ebenso wie die PS/2-Tastaturen von ClokePS2 (kext/ClokePS2).

Die Tabelle wird über ihren Anfang gefunden (a = 30, b = 48, ...), nicht über einen festen Offset;
nur Einträge, die 0 sind, werden gesetzt. Werte wie in Linux' hid-input.c.
"""
import struct
import sys

# HID-Usage: Linux-Tastencode
MISSING = {
    0x46: 99,   # Druck (KEY_SYSRQ)
    0x47: 70,   # Rollen
    0x48: 119,  # Pause
    0x49: 110,  # Einfg
    0x4a: 102,  # Pos1
    0x4b: 104,  # Bild hoch
    0x4c: 111,  # Entf
    0x4d: 107,  # Ende
    0x4e: 109,  # Bild runter
    0x53: 69,   # Num
    0x54: 98,   # Ziffernfeld /
    0x55: 55,   # *
    0x56: 74,   # -
    0x57: 78,   # +
    0x58: 96,   # Enter
    0x59: 79, 0x5a: 80, 0x5b: 81, 0x5c: 75, 0x5d: 76, 0x5e: 77, 0x5f: 71, 0x60: 72, 0x61: 73,  # 1-9
    0x62: 82,   # 0
    0x63: 83,   # ,
    0x64: 86,   # <>| (KEY_102ND)
    0x65: 127,  # Menü (KEY_COMPOSE)
    0x7f: 113,  # Ton aus
    0x80: 115,  # lauter
    0x81: 114,  # leiser
}
# Anfang der Tabelle: 0, 0, 0, 0, dann a b c d e f
HEAD = struct.pack('<10H', 0, 0, 0, 0, 30, 48, 46, 32, 18, 33)


def main(path):
    data = bytearray(open(path, 'rb').read())
    at = data.find(HEAD)
    if at < 0 or data.find(HEAD, at + 1) >= 0:
        sys.exit('inputfix.py: Tabelle usbToEvdev nicht eindeutig gefunden in %s' % path)
    changed = 0
    for usage, code in MISSING.items():
        o = at + 2 * usage
        old = struct.unpack_from('<H', data, o)[0]
        if old == 0:
            struct.pack_into('<H', data, o, code)
            changed += 1
        elif old != code:
            sys.exit('inputfix.py: Eintrag 0x%02x ist schon %d, nicht %d: andere Fassung?' % (usage, old, code))
    if changed:
        open(path, 'wb').write(data)
    print('puredarwininput: %d Tasten ergänzt (Tabelle bei 0x%x)' % (changed, at))


if __name__ == '__main__':
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    main(sys.argv[1])
