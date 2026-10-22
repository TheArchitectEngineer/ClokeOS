#!/usr/bin/env python3
"""Korrigiert PureDarwins Grafiktreiber im Kernel Collection (EFI/BOOT/kernel).

  kcfix.py <kernel>      patcht die Datei an Ort und Stelle (ein zweiter Aufruf ändert nichts)

Welche Korrektur, hängt von der Architektur des Kernel Collection ab:

x86_64: IOGOPFramebuffer, falsche Farben
----------------------------------------
Bei der PCI-Konfiguration verlegt IOPCIFamily die Konsole und schreibt ihre Adresse mit gesetztem
Bit 0 zurück (0x80000001 statt 0x80000000; bei XNU heißt das "physische Adresse").
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

PureDarwin hat den Fehler im Entwicklungszweig upstream-xnu-12377 schon behoben (Commit 867967e,
fbBase = bootDisplay.v_baseAddr & ~3).

arm64: IOVirtIOGPU, Kernel-Panik beim ersten Bild von Xorg
----------------------------------------------------------
Auf arm64 zeichnet Xorg über IOVirtIOGPU (virtio-gpu), nicht über IOGOPFramebuffer; der Farbfehler
oben tritt dort nicht auf. Dafür gerät der Kernel in Panik, sobald ein Programm das erste Bild
zeigt: IOVirtIOGPU::gpuPresent() ruft setConsoleDrawing(false), das die Kernel-Konsole mit
setConsoleInfo(NULL, kPEDisableScreen) abschalten will. Das landet in switch_to_serial_console(),
die cons_ops_index schreibt, und diese Variable liegt in PureDarwins arm64-Kernel in __DATA_CONST,
das nach dem Start schreibgeschützt ist ("Unexpected fault in kernel static region").

Die Korrektur ersetzt den einen Aufruf (blr x8) durch nop. setConsoleDrawing merkt sich den
Zustand weiter und schreibt mit vgpu_debug=1 weiter "kernel console paused for client"; die
Kernel-Konsole bleibt nur eingeschaltet. Ohne -v zeichnet sie ohnehin nichts, und Xorg zeigt sein
eigenes Bild (eigene Ressource als Scanout).

Mit einem Kernel Collection, in dem PureDarwin einen der Fehler behoben hat, findet das Skript den
alten Code nicht mehr. Es warnt dann nur und ändert nichts, damit brand.sh weiterbaut. Zeigt Xorg
danach trotzdem falsche Farben oder gerät der Kernel beim Start von Xorg in Panik, muss das Muster
hier angepasst werden.
"""
import struct
import sys

CPU_X86_64 = 0x01000007
CPU_ARM64 = 0x0100000c

PATCHES = {
    # IOGOPFramebuffer::start() aus PureDarwin 20.5 (Kernel Collection vom 23.08.2026, in
    # puredarwin-minimal.img und puredarwin.img gleich), ab dem virtuellen Aufruf getConsoleInfo.
    CPU_X86_64: dict(
        name='IOGOPFramebuffer: Framebuffer-Basis',
        context=bytes.fromhex('488bbd50ffffff' '488b07' '488db558ffffff' 'ff90e8080000'),
        original=bytes.fromhex('89854cffffff' '83bd4cffffff00' '742c'),
        patched=bytes.fromhex('4883a558fffffffc' '85c0' '742f' '0f1f00'),
        hint='Zeigt Xorg falsche Farben (Blau erscheint grün), muss kcfix.py an diesen Kernel '
             'angepasst werden.'),
    # IOVirtIOGPU::setConsoleDrawing(bool) aus PureDarwin 20.5 (Kernel Collection vom 23.08.2026,
    # in puredarwin-arm64-virt.img und puredarwin-arm64-virt-minimal.img gleich): Argumente für
    # getPlatform()->setConsoleInfo(NULL, on ? kPEEnableScreen : kPEDisableScreen), dann der Aufruf.
    #   mov w9,#7; mov w8,#6; mov w11,#1; str w11,[sp,#0xc]; and/ands w10; csel w2,w8,w9,ne;
    #   ldr x8,[x0]; ldr x8,[x8,#0x920]; mov x1,#0;  blr x8 -> nop
    CPU_ARM64: dict(
        name='IOVirtIOGPU: Kernel-Konsole beim ersten Bild',
        context=bytes.fromhex('e9008052' 'c8008052' '2b008052' 'eb0f00b9' '4a010012' '4a010072'
                              '0211891a' '080040f9' '089144f9' '010080d2'),
        original=bytes.fromhex('00013fd6'),
        patched=bytes.fromhex('1f2003d5'),
        hint='Gerät der Kernel beim Start von Xorg in Panik ("Unexpected fault in kernel static '
             'region"), muss kcfix.py an diesen Kernel angepasst werden.'),
}
for p in PATCHES.values():
    assert len(p['original']) == len(p['patched'])


def main(path):
    data = bytearray(open(path, 'rb').read())
    magic, cpu = struct.unpack_from('<II', data, 0)
    if magic != 0xfeedfacf or cpu not in PATCHES:
        print('kcfix.py: kein Kernel Collection für x86_64 oder arm64: %s' % path, file=sys.stderr)
        return 1
    p = PATCHES[cpu]
    ctx = p['context']
    if data.count(ctx + p['patched']) == 1:
        print('%s: schon korrigiert' % p['name'])
        return 0
    found = data.count(ctx + p['original'])
    if found != 1:
        print('WARNUNG: %s: bekannter Code %s, nichts geändert.\n'
              '  Vermutlich ein neueres Kernel Collection, in dem PureDarwin den Fehler schon behoben hat.\n'
              '  %s' % (p['name'], 'nicht gefunden' if not found else '%d-mal gefunden' % found, p['hint']),
              file=sys.stderr)
        return 0
    at = data.find(ctx + p['original']) + len(ctx)
    data[at:at + len(p['original'])] = p['patched']
    open(path, 'wb').write(data)
    print('%s korrigiert (Offset 0x%x)' % (p['name'], at))
    return 0


if __name__ == '__main__':
    if len(sys.argv) != 2:
        print(__doc__)
        sys.exit(2)
    sys.exit(main(sys.argv[1]))
