#!/usr/bin/env python3
"""Hilfen für recovery/build.sh.

  imagetool.py prune <wurzel> <liste> [--usb]   was aus dem System für die Wiederherstellung wegfällt;
      --usb streicht mehr (USB-Stick, recovery/usb.sh: die Wurzel muss als RAM-Disk mit den Abbildern
      unter die Grenze von XNU passen, siehe loader/xnu-loader.patch)
  imagetool.py owners <quelle.ext4> <wurzel> <befehle>   debugfs-Befehle, die Besitzer und Rechte
      aller Pfade von <wurzel> wie in <quelle.ext4> setzen (mke2fs -d übernimmt die des Macs)
  imagetool.py gpt <image> <esp> <root> <name> <guid>   startfähiges Image aus EFI-Partition und
      Wurzel; <guid> wird die Kennung der Wurzelpartition (der Loader startet mit boot-uuid=<guid>).
      <root> - : nur die EFI-Partition (USB-Medium, die Wurzel liegt dort als RAM-Disk)
  imagetool.py disk <image> <esp> <system> <recovery> <sys-guid> <rec-guid>   fertige Festplatte
      wie nach der Installation (recovery/disk.sh); die Boot-Argumente in <esp> setzt disk.sh

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

# Nur für den USB-Stick: Mesa und LLVM (CloakServer zeichnet mit cairo und XRender, ohne GL),
# Übersetzungen, Git, Wayland, Xvfb, Dokumentation
USB_PRUNE_DIRS = [
    'share/locale', 'usr/share/locale', 'usr/libexec/git-core', 'usr/share/info', 'usr/lib/meson',
    'usr/share/mesa-demos', 'share/misc', 'usr/share/misc', 'usr/lib/dri', 'lib/dri', 'share/man', 'usr/share/man',
    'share/doc', 'usr/share/doc', 'share/gtk-doc', 'usr/share/gtk-doc',
]
USB_PRUNE_PREFIXES = [
    'usr/lib/libLLVM', 'usr/lib/libgallium', 'usr/lib/libvulkan', 'usr/lib/libEGL', 'usr/lib/libGLES',
    'usr/lib/libGLX_mesa', 'usr/lib/libgbm', 'usr/lib/libOSMesa', 'lib/libwlroots', 'bin/Xvfb', 'usr/bin/Xvfb',
    'usr/bin/git', 'bin/git',
    # SSH: sshd, ssh-keygen und sftp-server bleiben (entfernte Anmeldung); jeder Helfer hier hat
    # OpenSSL statisch eingebaut (je 6 MB)
    'bin/ssh-keyscan', 'bin/ssh-agent', 'bin/ssh-add', 'bin/sftp', 'bin/scp', 'libexec/ssh-sk-helper',
    'libexec/ssh-pkcs11-helper', 'libexec/ssh-keysign', 'bin/openssl', 'bin/curl', 'bin/xterm',
]

# Ohne diese startet die Wiederherstellung nicht oder kann nichts installieren
ESSENTIAL = [
    'sbin/launchd', 'bin/zsh', 'System/Library/CoreServices/CloakServer/CloakServer',
    'System/Library/CoreServices/CloakServer/cloakmenu', 'usr/libexec/clokeos-installer', 'bin/xfce4-terminal',
    'usr/bin/Xorg', 'bin/xinit', 'sbin/sshd',
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


def prune(root, listfile, usb=False):
    root = root.rstrip('/')
    gone = set()
    dirs = PRUNE_DIRS + (USB_PRUNE_DIRS if usb else [])
    prefixes = PRUNE_PREFIXES + (USB_PRUNE_PREFIXES if usb else [])
    for d in dirs:
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
            if any(p.startswith(x) for x in prefixes) or any(n.endswith(s) for s in PRUNE_SUFFIXES):
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


ESP_TYPE = 'C12A7328-F81F-11D2-BA4B-00A0C93EC93B'
LINUX_TYPE = '0FC63DAF-8483-4772-8E79-3D69D8477DE4'


def write_gpt(image, parts):
    """Image mit schützendem MBR, GPT und Sicherungs-GPT am Ende; parts: (Typ, Datei, GPT-Name,
    Kennung), jede Partition auf ganze MiB, die erste ab 1 MiB. Nullblöcke bleiben Löcher."""
    bs = 512
    mib = 1 << 20
    layout = []
    first = mib // bs
    for t, path, pname, guid in parts:
        blocks = (os.path.getsize(path) + mib - 1) // mib * mib // bs
        layout.append((t, path, first, blocks, pname, guid))
        first += blocks
    total = first * bs + mib
    last = total // bs - 1
    entries = bytearray(128 * 128)
    for i, (t, path, first, count, pname, guid) in enumerate(layout):
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
    zero = bytes(8 * mib)
    with open(image, 'wb') as f:
        f.truncate(total)
        f.write(mbr)
        f.write(header(1, last, 2))
        f.write(entries)
        f.seek((last - tab_blocks) * bs)
        f.write(entries)
        f.write(header(last, 1, last - tab_blocks))
        for t, path, first, count, pname, guid in layout:
            pos = first * bs
            with open(path, 'rb') as src:
                while True:
                    chunk = src.read(8 * mib)
                    if not chunk:
                        break
                    if chunk != zero[:len(chunk)]:
                        f.seek(pos)
                        f.write(chunk)
                    pos += len(chunk)
    print('%s: %.0f MB' % (image, total / 1e6))


def gpt(image, esp, rootfs, name, root_guid):
    """USB-Stick: EFI-Partition, dahinter die Wurzel (ext4) mit GPT-Name name. Ohne Wurzel ('-') trägt
    die EFI-Partition den Namen: "ClokeOS Installer" blendet den Stick im Installer als Ziel aus."""
    if rootfs == '-':
        write_gpt(image, [(ESP_TYPE, esp, name or 'EFI System Partition', uuid.uuid4())])
    else:
        write_gpt(image, [(ESP_TYPE, esp, 'EFI System Partition', uuid.uuid4()),
                          (LINUX_TYPE, rootfs, name, uuid.UUID(root_guid))])


def disk(image, esp, system, recovery, sys_guid, rec_guid):
    """Fertige Festplatte wie nach clokeos-installer install: EFI-Partition, System, Wiederherstellung"""
    write_gpt(image, [(ESP_TYPE, esp, 'EFI System Partition', uuid.uuid4()),
                      (LINUX_TYPE, system, 'ClokeOS', uuid.UUID(sys_guid)),
                      (LINUX_TYPE, recovery, 'ClokeOS Recovery', uuid.UUID(rec_guid))])


def owners(src, root, cmdfile):
    """Besitzer und Rechte aus <src> (ein debugfs-Lauf mit stat für alle Pfade) als sif-Befehle"""
    import subprocess
    root = root.rstrip('/')
    paths = ['/']
    for dp, dns, fns in os.walk(root):
        rel = dp[len(root):] or ''
        for n in dns + fns:
            paths.append(rel + '/' + n)
    q = lambda p: '"' + p.replace('"', '\\"') + '"'
    script = ''.join('stat %s\n' % q(p) for p in paths)
    debugfs = os.environ.get('DEBUGFS', 'debugfs')
    out = subprocess.run([debugfs, '-f', '-', src], input=script, capture_output=True, text=True).stdout
    # Je stat: "Inode: N   Type: …    Mode:  0755 …" und "User: u   Group: g …"; der Pfad steht im
    # Befehl davor ("debugfs: stat …")
    info, cur = {}, None
    for line in out.splitlines():
        if line.startswith('debugfs: stat '):
            cur = line[len('debugfs: stat '):].strip().strip('"')
        elif cur and line.startswith('Inode:'):
            m = line.split('Mode:')[1].split()[0] if 'Mode:' in line else None
            info.setdefault(cur, {})['mode'] = m
        elif cur and line.startswith('User:'):
            parts = line.split()
            info.setdefault(cur, {})['uid'] = parts[1]
            info[cur]['gid'] = parts[3]
    n = 0
    with open(cmdfile, 'w') as f:
        for p in paths:
            i = info.get(p)
            if not i or 'uid' not in i: # neu (Overlay der Wiederherstellung): root, Rechte der Datei
                i = {'uid': '0', 'gid': '0', 'mode': '%o' % (os.lstat(root + p).st_mode & 0o7777)}
            f.write('sif %s uid %s\nsif %s gid %s\n' % (q(p), i['uid'], q(p), i['gid']))
            # Rechte samt setuid und sticky; der Dateityp bleibt, wie mke2fs ihn angelegt hat
            if i.get('mode') and not os.path.islink(root + p):
                typ = 0o170000 & os.lstat(root + p).st_mode if p != '/' else 0o040000
                f.write('sif %s mode 0%o\n' % (q(p), typ | int(i['mode'], 8)))
            n += 1
    print('Besitzer und Rechte für %d von %d Pfaden' % (n, len(paths)))


if __name__ == '__main__':
    if len(sys.argv) in (4, 5) and sys.argv[1] == 'prune' and sys.argv[4:] in ([], ['--usb']):
        prune(sys.argv[2], sys.argv[3], usb=sys.argv[4:] == ['--usb'])
    elif len(sys.argv) == 5 and sys.argv[1] == 'owners':
        owners(*sys.argv[2:])
    elif len(sys.argv) == 7 and sys.argv[1] == 'gpt':
        gpt(*sys.argv[2:])
    elif len(sys.argv) == 8 and sys.argv[1] == 'disk':
        disk(*sys.argv[2:])
    else:
        sys.exit(__doc__)
