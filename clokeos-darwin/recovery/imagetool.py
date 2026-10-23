#!/usr/bin/env python3
"""Hilfen für recovery/build.sh.

  imagetool.py prune <wurzel> <liste>     was aus dem System für die Wiederherstellung wegfällt
  imagetool.py gpt <image> <esp> <root> <name> <guid>   startfähiges Image aus EFI-Partition und
      Wurzel; <guid> wird die Kennung der Wurzelpartition (der Loader startet mit boot-uuid=<guid>)

prune bekommt den ausgepackten Wurzelbaum des Desktop-Systems (debugfs rdump) und schreibt die
Pfade, die wegfallen, in <liste> (Ordner nach ihrem Inhalt, tiefste zuerst). Weg kommen Wine,
Compiler und Entwicklerdateien (PRUNE). Danach liest es die Ladebefehle aller übrigen Mach-O-Dateien:
Braucht eine eine entfernte Bibliothek, fällt sie mit weg, und so weiter. Trifft das ein Programm
aus ESSENTIAL, bricht es ab, statt eine Wiederherstellung zu bauen, die nicht startet.
"""
import os
import struct
import sys
import uuid
import zlib

# Ordner und Dateien, die die Wiederherstellung nicht braucht
PRUNE_DIRS = [
    'usr/lib/wine', 'usr/share/wine', 'usr/lib/python3.13', 'usr/lib/clang', 'usr/share/cmake-4.1',
    'usr/include', 'include', 'pd-xpc-dev', 'pd-guest-headers', 'System/Library/Kernels',
    'share/man', 'usr/share/man', 'share/doc', 'usr/share/doc', 'share/gtk-doc', 'lib/dillo',
    'usr/lib/pkgconfig', 'lib/pkgconfig', 'share/aclocal', 'usr/share/aclocal', 'lib/cmake', 'usr/lib/cmake',
]
PRUNE_PREFIXES = [  # Dateinamen in bin-Ordnern und Bibliotheken
    'usr/bin/wine', 'bin/wine', 'usr/bin/cmake', 'usr/bin/ctest', 'usr/bin/cpack', 'usr/bin/clang',
    'usr/bin/llvm-', 'usr/bin/lld', 'usr/bin/ld.lld', 'usr/bin/ld64', 'usr/lib/libclang', 'usr/lib/libpython',
    'bin/dillo', 'bin/dpid', 'usr/bin/python', 'bin/python', 'usr/bin/Xwayland', 'bin/sway', 'usr/bin/sway',
]
PRUNE_SUFFIXES = ['.a', '.la', '.h', '.hpp', '.pc']

# Ohne diese startet die Wiederherstellung nicht oder kann nichts installieren
ESSENTIAL = [
    'sbin/launchd', 'bin/zsh', 'System/Library/CoreServices/CloakServer/CloakServer',
    'System/Library/CoreServices/CloakServer/cloakmenu', 'usr/libexec/clokeos-installer', 'bin/xfce4-terminal',
]

LOAD_CMDS = {0xc, 0x80000018, 0x8000001f, 0x80000023}  # LC_LOAD_DYLIB, _WEAK_, REEXPORT_, _UPWARD_


def resolve(root, path, depth=0):
    """Pfad im Baum root wie im Image auflösen (absolute Symlinks zeigen ins Image, nicht auf den Mac)"""
    parts = [p for p in path.split('/') if p]
    cur = ''
    for i, p in enumerate(parts):
        nxt = cur + '/' + p
        full = root + nxt
        if os.path.islink(full):
            if depth > 20:
                return None
            target = os.readlink(full)
            base = target if target.startswith('/') else os.path.normpath(os.path.dirname(nxt) + '/' + target)
            rest = '/'.join(parts[i + 1:])
            return resolve(root, base + ('/' + rest if rest else ''), depth + 1)
        cur = nxt
    return cur if os.path.lexists(root + cur) else None


def dylibs(path):
    """Installationsnamen, die eine Mach-O-Datei lädt (dünne Dateien, 64 Bit)"""
    try:
        with open(path, 'rb') as f:
            head = f.read(32)
            if len(head) < 32 or struct.unpack('<I', head[:4])[0] != 0xfeedfacf:
                return None
            ncmds, size = struct.unpack('<II', head[16:24])
            data = f.read(size)
    except OSError:
        return None
    out, off = [], 0
    for _ in range(ncmds):
        if off + 8 > len(data):
            break
        cmd, cmdsize = struct.unpack('<II', data[off:off + 8])
        if cmd in LOAD_CMDS:
            name_off = struct.unpack('<I', data[off + 8:off + 12])[0]
            name = data[off + name_off:off + cmdsize].split(b'\0', 1)[0].decode('utf-8', 'replace')
            out.append(name)
        off += cmdsize
    return out


def prune(root, listfile):
    root = root.rstrip('/')
    gone = set()
    for d in PRUNE_DIRS:
        if os.path.isdir(root + '/' + d) and not os.path.islink(root + '/' + d):
            for dp, dns, fns in os.walk(root + '/' + d):
                rel = dp[len(root):]
                gone.add(rel)
                for n in fns + [x for x in dns if os.path.islink(dp + '/' + x)]:
                    gone.add(rel + '/' + n)
    for dp, dns, fns in os.walk(root):
        rel = dp[len(root):]
        for n in fns + [x for x in dns if os.path.islink(dp + '/' + x)]:
            p = (rel + '/' + n).lstrip('/')
            if any(p.startswith(x) for x in PRUNE_PREFIXES) or any(n.endswith(s) for s in PRUNE_SUFFIXES):
                gone.add('/' + p)

    def present(path):
        r = resolve(root, path)
        return r is not None and r not in gone

    # Programme und Bibliotheken, die eine entfernte Bibliothek laden, fallen mit weg
    machos = {}
    for dp, dns, fns in os.walk(root):
        for n in fns:
            full = dp + '/' + n
            if os.path.islink(full):
                continue
            libs = dylibs(full)
            if libs is not None:
                machos[full[len(root):]] = libs
    changed = True
    while changed:
        changed = False
        for p, libs in machos.items():
            if p in gone:
                continue
            missing = [l for l in libs if l.startswith('/') and not present(l)]
            if missing:
                gone.add(p)
                changed = True
                if p.lstrip('/') in ESSENTIAL:
                    sys.exit('Wiederherstellung unvollständig: %s braucht %s' % (p, missing[0]))
                print('fällt mit weg: %s (braucht %s)' % (p, missing[0]), file=sys.stderr)
    for e in ESSENTIAL:
        if not present('/' + e):
            sys.exit('Wiederherstellung unvollständig: %s fehlt' % e)
    # Symlinks auf Entferntes ebenfalls weg
    for dp, dns, fns in os.walk(root):
        for n in fns + dns:
            full = dp + '/' + n
            p = full[len(root):]
            if os.path.islink(full) and p not in gone and resolve(root, p) is None:
                gone.add(p)
    with open(listfile, 'w') as f:
        for p in sorted(gone, key=lambda x: (-x.count('/'), x)):
            f.write(p + '\n')
    size = 0
    for p in gone:
        full = root + p
        if os.path.isfile(full) and not os.path.islink(full):
            size += os.path.getsize(full)
    print('fällt weg: %d Einträge, %.0f MB' % (len(gone), size / 1e6))


def gpt(image, esp, rootfs, name, root_guid):
    """Image: 1 MiB frei, EFI-Partition, Wurzel (ext4) mit GPT-Name name, Sicherungs-GPT am Ende"""
    bs = 512
    mib = 1 << 20
    esp_size = os.path.getsize(esp)
    root_size = os.path.getsize(rootfs)
    esp_first = mib // bs
    esp_blocks = (esp_size + mib - 1) // mib * mib // bs
    root_first = esp_first + esp_blocks
    root_blocks = (root_size + mib - 1) // mib * mib // bs
    total = (root_first + root_blocks) * bs + mib
    last = total // bs - 1
    entries = bytearray(128 * 128)
    parts = [
        ('C12A7328-F81F-11D2-BA4B-00A0C93EC93B', esp_first, esp_blocks, 'EFI System Partition', uuid.uuid4()),
        ('0FC63DAF-8483-4772-8E79-3D69D8477DE4', root_first, root_blocks, name, uuid.UUID(root_guid)),
    ]
    for i, (t, first, count, pname, guid) in enumerate(parts):
        e = 128 * i
        entries[e:e + 16] = uuid.UUID(t).bytes_le
        entries[e + 16:e + 32] = guid.bytes_le
        entries[e + 32:e + 48] = struct.pack('<QQ', first, first + count - 1)
        n = pname.encode('utf-16-le')
        entries[e + 56:e + 56 + len(n)] = n
    tab_crc = zlib.crc32(entries) & 0xffffffff
    disk_guid = uuid.uuid4().bytes_le
    tab_blocks = len(entries) // bs

    def header(my, alt, tab_lba):
        h = bytearray(92)
        h[0:8] = b'EFI PART'
        struct.pack_into('<IIII', h, 8, 0x10000, 92, 0, 0)
        struct.pack_into('<QQQQ', h, 24, my, alt, 2 + tab_blocks, last - 1 - tab_blocks)
        h[56:72] = disk_guid
        struct.pack_into('<QIII', h, 72, tab_lba, 128, 128, tab_crc)
        struct.pack_into('<I', h, 16, zlib.crc32(h) & 0xffffffff)
        return bytes(h) + bytes(bs - 92)

    mbr = bytearray(bs)
    mbr[446:462] = bytes([0, 0, 2, 0, 0xee, 0xff, 0xff, 0xff]) + struct.pack('<II', 1, min(last, 0xffffffff))
    mbr[510:512] = b'\x55\xaa'
    with open(image, 'wb') as f:
        f.truncate(total)
        f.write(mbr)
        f.write(header(1, last, 2))
        f.write(entries)
        f.seek((last - tab_blocks) * bs)
        f.write(entries)
        f.write(header(last, 1, last - tab_blocks))
        for path, first in ((esp, esp_first), (rootfs, root_first)):
            f.seek(first * bs)
            with open(path, 'rb') as src:
                while True:
                    chunk = src.read(8 * mib)
                    if not chunk:
                        break
                    f.write(chunk)
    print('%s: %.0f MB' % (image, total / 1e6))


if __name__ == '__main__':
    if len(sys.argv) == 4 and sys.argv[1] == 'prune':
        prune(sys.argv[2], sys.argv[3])
    elif len(sys.argv) == 7 and sys.argv[1] == 'gpt':
        gpt(*sys.argv[2:])
    else:
        sys.exit(__doc__)
