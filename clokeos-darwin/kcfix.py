#!/usr/bin/env python3
"""Korrigiert PureDarwins Grafiktreiber IOGOPFramebuffer im Kernel Collection (EFI/BOOT/kernel).

  kcfix.py <kernel>      patcht die Datei an Ort und Stelle (ein zweiter Aufruf ändert nichts)

Der Fehler: Bei der PCI-Konfiguration verlegt IOPCIFamily die Konsole und schreibt ihre Adresse
mit gesetztem Bit 0 zurück (0x80000001 statt 0x80000000; bei XNU heißt das "physische Adresse").
IOGOPFramebuffer::start() übernimmt diese Adresse ungefiltert als Basis des Framebuffers. Damit
liegen /dev/fb0 und die VRAM-Abbildung, in die Xorg zeichnet, ein Byte neben dem Bildschirm:
Blau erscheint grün, Weiß gelb, Rot schwarz. Das passiert auch mit PureDarwins eigenem Loader.

Die Korrektur ersetzt 15 Byte direkt nach dem Aufruf von getConsoleInfo() durch gleich lange
Befehle, die die Basisadresse auf 4 Byte abrunden (der Treiber ist ohne Optimierung übersetzt,
dort ist Platz):

    vorher  mov %eax,-0xb4(%rbp); cmpl $0,-0xb4(%rbp); je ok
    nachher andq $-4,-0xa8(%rbp); test %eax,%eax; je ok; nop

-0xa8(%rbp) ist bootDisplay.v_baseAddr. Der Fehlercode in -0xb4(%rbp) wird nur noch für eine
Debug-Meldung (gop_debug=1) gelesen, die dann einen falschen Wert zeigt.
"""
import sys

# IOGOPFramebuffer::start() aus PureDarwin 20.5 (Kernel Collection vom 23.08.2026, in
# puredarwin-minimal.img und puredarwin.img gleich), ab dem virtuellen Aufruf getConsoleInfo.
CONTEXT = bytes.fromhex('488bbd50ffffff' '488b07' '488db558ffffff' 'ff90e8080000')
ORIGINAL = bytes.fromhex('89854cffffff' '83bd4cffffff00' '742c')
PATCHED = bytes.fromhex('4883a558fffffffc' '85c0' '742f' '0f1f00')
assert len(ORIGINAL) == len(PATCHED) == 15


def main(path):
    data = bytearray(open(path, 'rb').read())
    if data.count(CONTEXT + PATCHED) == 1:
        print('IOGOPFramebuffer: schon korrigiert')
        return 0
    if data.count(CONTEXT + ORIGINAL) != 1:
        print('IOGOPFramebuffer: Code nicht gefunden, anderer Kernel? Nichts geändert.', file=sys.stderr)
        return 1
    at = data.find(CONTEXT + ORIGINAL) + len(CONTEXT)
    data[at:at + len(ORIGINAL)] = PATCHED
    open(path, 'wb').write(data)
    print('IOGOPFramebuffer: Framebuffer-Basis korrigiert (Offset 0x%x)' % at)
    return 0


if __name__ == '__main__':
    if len(sys.argv) != 2:
        print(__doc__)
        sys.exit(2)
    sys.exit(main(sys.argv[1]))
