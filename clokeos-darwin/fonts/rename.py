#!/usr/bin/env python3
"""Benennt die Schnitte von Nova Sans (Inter-Umrisse, SIL OFL 1.1) in Cloke Sans um.

  fonts/rename.py <ordner mit NovaSans-*.otf> <ausgabeordner>

Geändert wird nur die name-Tabelle (Familie, Schnitt, voller Name, PostScript-Name, eindeutige
Kennung); Copyright, Markenhinweis, Lizenz und Lizenz-URL von Inter bleiben, wie es die OFL
verlangt. Die Umrisse und alle übrigen Tabellen bleiben Byte für Byte gleich, die Prüfsummen der
Tabellen und checkSumAdjustment im head werden neu berechnet. Ohne fontTools, damit es mit dem
Python des Macs läuft.
"""
import os
import struct
import sys

FAMILY = 'Cloke Sans'
PS = 'ClokeSans'
# Datei-Schnitt: (Schnittname, alte Familie für ID 1, Schnitt für ID 2)
STYLES = {
    'Regular': ('Regular', FAMILY, 'Regular'),
    'Medium': ('Medium', FAMILY + ' Medium', 'Regular'),
    'SemiBold': ('SemiBold', FAMILY + ' SemiBold', 'Regular'),
    'Bold': ('Bold', FAMILY, 'Bold'),
}


def checksum(data):
    data += b'\0' * (-len(data) % 4)
    return sum(struct.unpack('>%dI' % (len(data) // 4), data)) & 0xffffffff


def new_names(style, version):
    sub, fam1, sub2 = STYLES[style]
    return {
        1: fam1,
        2: sub2,
        3: '%s %s %s' % (FAMILY, sub, version),
        4: '%s %s' % (FAMILY, sub) if sub != 'Regular' else FAMILY + ' Regular',
        6: '%s-%s' % (PS, sub),
        16: FAMILY,
        17: sub,
    }


def rename(src, dst, style):
    d = open(src, 'rb').read()
    sfnt, ntab = struct.unpack('>IH', d[:6])
    tables = {}
    for i in range(ntab):
        tag, _, off, ln = struct.unpack('>4sIII', d[12 + 16 * i:28 + 16 * i])
        tables[tag] = bytearray(d[off:off + ln])

    # name-Tabelle lesen
    name = tables[b'name']
    fmt, cnt, so = struct.unpack('>HHH', name[:6])
    recs = []
    for i in range(cnt):
        p, e, l, nid, ln, o = struct.unpack('>6H', name[6 + 12 * i:18 + 12 * i])
        recs.append([p, e, l, nid, bytes(name[so + o:so + o + ln])])
    version = '1.0'
    for p, e, l, nid, s in recs:
        if nid == 3 and p == 3:
            version = s.decode('utf-16-be').split()[-1]
    repl = new_names(style, version)
    out = []
    for p, e, l, nid, s in recs:
        if nid in (21, 22):  # WWS-Namen: nicht mehr nötig, nannten teils noch Inter
            continue
        if nid in repl:
            s = repl[nid].encode('utf-16-be' if p in (0, 3) else 'mac_roman')
        out.append((p, e, l, nid, s))
    present = {(p, e, l, nid) for p, e, l, nid, _ in out}
    for nid in (16, 17):  # typografische Namen auf Windows sicher setzen
        if (3, 1, 0x409, nid) not in present:
            out.append((3, 1, 0x409, nid, repl[nid].encode('utf-16-be')))
    out.sort(key=lambda r: r[:4])
    storage = b''
    hdr = struct.pack('>HHH', 0, len(out), 6 + 12 * len(out))
    for p, e, l, nid, s in out:
        hdr += struct.pack('>6H', p, e, l, nid, len(s), len(storage))
        storage += s
    tables[b'name'] = bytearray(hdr + storage)

    # Schrift neu zusammensetzen
    tables[b'head'][8:12] = b'\0\0\0\0'
    tags = sorted(tables)
    n = len(tags)
    es = 1
    while es * 2 <= n:
        es *= 2
    search = es * 16
    head = struct.pack('>IHHHH', sfnt, n, search, es.bit_length() - 1, n * 16 - search)
    off = 12 + 16 * n
    body = b''
    for tag in tags:
        t = bytes(tables[tag])
        head += struct.pack('>4sIII', tag, checksum(t), off + len(body), len(t))
        body += t + b'\0' * (-len(t) % 4)
    font = bytearray(head + body)
    adj = (0xb1b0afba - checksum(bytes(font))) & 0xffffffff
    hoff = struct.unpack('>I', head[12 + 16 * tags.index(b'head') + 8:12 + 16 * tags.index(b'head') + 12])[0]
    font[hoff + 8:hoff + 12] = struct.pack('>I', adj)
    open(dst, 'wb').write(font)


def main(srcdir, outdir):
    os.makedirs(outdir, exist_ok=True)
    for style in STYLES:
        src = os.path.join(srcdir, 'NovaSans-%s.otf' % style)
        dst = os.path.join(outdir, '%s-%s.otf' % (PS, style))
        rename(src, dst, style)
        print(dst)


if __name__ == '__main__':
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    main(sys.argv[1], sys.argv[2])
