#!/usr/bin/env python3
"""Biegt die Ladebefehle einer Mach-O-Bibliothek von sechs Frameworks auf eine Ersatzbibliothek um.

  repoint.py <datei.so> <neuer-pfad>

Der Pfad wird an Ort und Stelle überschrieben (er muss kürzer sein als der alte); die Symbole
werden über die Reihenfolge der Ladebefehle gebunden, deshalb bleibt sonst alles unverändert.
"""
import struct
import sys

LC_LOAD_DYLIB = 0xC
FRAMEWORKS = (b'DiskArbitration', b'SystemConfiguration', b'CoreServices', b'Security', b'IOKit', b'CoreFoundation')

path, new = sys.argv[1], sys.argv[2].encode()
data = bytearray(open(path, 'rb').read())
magic, _, _, _, ncmds = struct.unpack_from('<IiiII', data, 0)
assert magic == 0xFEEDFACF, 'kein 64-Bit-Mach-O'
off = 32
changed = 0
for _ in range(ncmds):
    cmd, size = struct.unpack_from('<II', data, off)
    if cmd == LC_LOAD_DYLIB:
        name_off, = struct.unpack_from('<I', data, off + 8)
        raw = bytes(data[off + name_off:off + size])
        name = raw.split(b'\0', 1)[0]
        if any(b'/' + f + b'.framework/' in name for f in FRAMEWORKS):
            assert len(new) < len(name), 'neuer Pfad zu lang'
            data[off + name_off:off + size] = new + b'\0' * (size - name_off - len(new))
            # Kompatibilitätsversion der Ersatzbibliothek ist 1000.0.0, die alte darf nicht größer sein
            changed += 1
    off += size
assert changed, 'kein Framework-Ladebefehl gefunden'
open(path, 'wb').write(data)
print(f'{path}: {changed} Ladebefehle umgebogen')
