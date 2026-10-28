#!/usr/bin/env python3
"""Trägt in ein Mach-O-Dylib ein weiteres LC_REEXPORT_DYLIB ein (hinten, damit sich die Nummern der
vorhandenen Abhängigkeiten nicht ändern). Der Platz dafür ist in PureDarwins libSystem.B.dylib da.

  reexport.py <dylib> <install-name>      ändert die Datei an Ort und Stelle
"""
import struct
import sys

LC_REEXPORT_DYLIB = 0x8000001F
MH_MAGIC_64 = 0xFEEDFACF

path, name = sys.argv[1], sys.argv[2].encode() + b'\0'
d = bytearray(open(path, 'rb').read())
magic, cpu, sub, ftype, ncmds, sizeofcmds, flags, _ = struct.unpack_from('<8I', d, 0)
if magic != MH_MAGIC_64:
    sys.exit('kein 64-Bit-Mach-O: ' + path)

off, first_section, have = 32, len(d), False
for _ in range(ncmds):
    cmd, size = struct.unpack_from('<II', d, off)
    if cmd == 0x19:  # LC_SEGMENT_64
        nsects = struct.unpack_from('<I', d, off + 64)[0]
        for i in range(nsects):
            so = off + 72 + 80 * i
            offset = struct.unpack_from('<I', d, so + 48)[0]
            if offset:
                first_section = min(first_section, offset)
    elif cmd in (0xC, LC_REEXPORT_DYLIB, 0x80000018):
        nameoff = struct.unpack_from('<I', d, off + 8)[0]
        if bytes(d[off + nameoff:off + size]).split(b'\0')[0] == name[:-1]:
            have = True
    off += size
if have:
    print('schon eingetragen:', name[:-1].decode())
    sys.exit(0)

cmdsize = (24 + len(name) + 7) & ~7
end = 32 + sizeofcmds
if end + cmdsize > first_section:
    sys.exit('kein Platz für das Ladekommando')
d[end:end + cmdsize] = struct.pack('<6I', LC_REEXPORT_DYLIB, cmdsize, 24, 2, 0x10000, 0x10000) + name.ljust(cmdsize - 24, b'\0')
struct.pack_into('<II', d, 16, ncmds + 1, sizeofcmds + cmdsize)
open(path, 'wb').write(d)
print('eingetragen:', name[:-1].decode())
