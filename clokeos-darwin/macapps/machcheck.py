#!/usr/bin/env python3
"""Prüft auf dem Mac, ob ein Mac-Programm unter ClokeOS laden kann, ohne die VM zu starten.

    macapps/machcheck.py PROGRAMM [--image IMG] [--overlay DIR ...] [--frameworks DIR ...] [--deep] [--alle]

PROGRAMM ist ein Mach-O (Programm oder dylib) oder ein .app-Bundle. Das Werkzeug geht die
Abhängigkeiten wie dyld durch (LC_LOAD_DYLIB, Re-Exporte) und meldet
  - Bibliotheken, die es auf ClokeOS nicht gibt,
  - importierte Symbole, die die genannte Bibliothek (samt Re-Exporten) nicht exportiert.
Systembibliotheken kommen aus dem PureDarwin-Image (per debugfs, zwischengespeichert unter
~/ClokeOS-work/sysroot/…/root) oder zuerst aus --overlay-Ordnern, die wie das Gastsystem aufgebaut
sind (z. B. die Foundation-Probe). --frameworks DIR wirkt wie DYLD_FRAMEWORK_PATH im Gast: ein
Framework wird zuerst als DIR/Name.framework/… gesucht, egal unter welchem Pfad es installiert
ist (z. B. ~/ClokeOS-work/out/macapps/root/Library/ClokeOS/MacFrameworks). Mit --deep werden auch die Importe der geladenen Bibliotheken
geprüft. Benutzt dyld_info und otool aus Xcode bzw. den Command Line Tools.
"""
import argparse
import os
import plistlib
import re
import struct
import subprocess
import sys
import uuid

HERE = os.path.dirname(os.path.abspath(__file__))
WORK = os.environ.get("CLOKEOS_WORK", os.path.expanduser("~/ClokeOS-work"))
DEBUGFS = "/opt/homebrew/opt/e2fsprogs/sbin/debugfs"
GUID_LINUX_FS = uuid.UUID("0FC63DAF-8483-4772-8E79-3D69D8477DE4")
ARCH = "x86_64"


def run(*cmd):
    return subprocess.run(cmd, capture_output=True, text=True, errors="replace").stdout


def root_offset(img):
    with open(img, "rb") as f:
        f.seek(512)
        hdr = f.read(92)
        if hdr[:8] != b"EFI PART":
            sys.exit("keine GPT in " + img)
        lba, count, size = struct.unpack_from("<QII", hdr, 72)
        f.seek(lba * 512)
        for _ in range(count):
            e = f.read(size)
            if uuid.UUID(bytes_le=e[:16]) == GUID_LINUX_FS:
                return struct.unpack_from("<Q", e, 32)[0] * 512
    sys.exit("keine Linux-Partition in " + img)


class Guest:
    """Dateien des Gastsystems: zuerst die Overlays, dann das Image (per debugfs)."""

    def __init__(self, img, overlays, cache):
        self.overlays = overlays
        self.cache = cache
        self.fs = f"{img}?offset={root_offset(img)}" if img else None

    def _debugfs(self, req):
        return subprocess.run([DEBUGFS, "-R", req, self.fs], capture_output=True).stdout

    def _link(self, path):
        out = self._debugfs(f"stat {path}").decode(errors="replace")
        if "Type: symlink" not in out:
            return None if "Inode:" in out else False
        m = re.search(r'Fast link dest: "(.*)"', out)
        if m:
            return m.group(1)
        return self._debugfs(f"cat {path}").decode(errors="replace").rstrip("\0\n")

    def resolve(self, path, depth=0):
        """Liefert den Pfad auf dem Mac oder None, wenn die Datei im Gast fehlt."""
        for ov in self.overlays:
            p = os.path.join(ov, path.lstrip("/"))
            if os.path.isfile(p):
                return p
        if not self.fs or depth > 16:
            return None
        # Symlinks in jedem Pfadteil auflösen (debugfs folgt dem letzten nicht).
        parts, cur = [p for p in path.split("/") if p], ""
        for i, part in enumerate(parts):
            nxt = cur + "/" + part
            link = self._link(nxt)
            if link is False:
                return None
            if link:
                target = link if link.startswith("/") else os.path.normpath(cur + "/" + link)
                return self.resolve(target + "/" + "/".join(parts[i + 1:]) if i + 1 < len(parts) else target,
                                    depth + 1)
            cur = nxt
        dest = os.path.join(self.cache, cur.lstrip("/"))
        if not os.path.isfile(dest):
            os.makedirs(os.path.dirname(dest), exist_ok=True)
            subprocess.run([DEBUGFS, "-R", f"dump {cur} {dest}", self.fs], capture_output=True)
        return dest if os.path.isfile(dest) and os.path.getsize(dest) > 0 else None


def short_name(install):
    """Name, mit dem dyld_info die Herkunft eines Imports nennt: AppKit, libobjc, libSystem."""
    m = re.search(r"/([^/]+)\.framework/", install)
    if m:
        return m.group(1)
    return os.path.basename(install).split(".")[0]


class Image:
    def __init__(self, path):
        self.path = path
        lines = run("otool", "-arch", ARCH, "-L", path).splitlines()[1:]
        self.id = None
        if run("otool", "-arch", ARCH, "-hv", path).find("DYLIB") >= 0 and lines:
            self.id = lines[0].split(" (")[0].strip()
            lines = lines[1:]
        self.deps = []  # (install name, reexport, weak)
        for l in lines:
            name = l.split(" (")[0].strip()
            self.deps.append((name, "reexport" in l, "weak" in l))
        self.rpaths = re.findall(r"cmd LC_RPATH\n\s+cmdsize \d+\n\s+path (.+?) \(offset", run("otool", "-arch", ARCH, "-l", path))
        self._exports = None
        self._imports = None

    def exports(self):
        if self._exports is None:
            ex, rex = set(), []
            for l in run("dyld_info", "-arch", ARCH, "-exports", self.path).splitlines():
                # "0x00003520  _acos", "[re-export] __Block_copy (from libdyld)"; dyld_stub_binder hat kein "_"
                m = re.match(r"\s+(?:0x[0-9A-Fa-f]+\s+(?:\[[^\]]*\]\s+)*|(?:\[[^\]]*\]\s+)+)(\S+)", l)
                if m:
                    ex.add(m.group(1))
            self._exports = ex
        return self._exports

    def imports(self):
        if self._imports is None:
            imp = []
            for l in run("dyld_info", "-arch", ARCH, "-imports", self.path).splitlines():
                m = re.match(r"\s+(?:0x[0-9A-Fa-f]+\s+)?(\S+)\s+(\[weak-import\]\s+)?\(from ([^)]+)\)", l)   # neuere dyld_info setzt die Adresse davor
                if m:
                    imp.append((m.group(1), m.group(3), bool(m.group(2)) or "weak" in l))
            self._imports = imp
        return self._imports


def main():
    ap = argparse.ArgumentParser(description="Prüft, ob ein Mac-Programm unter ClokeOS laden kann.")
    ap.add_argument("programm")
    ap.add_argument("--image", default=None, help="PureDarwin-/ClokeOS-Image (Standard: puredarwin.img des Desktop-Builds)")
    ap.add_argument("--overlay", action="append", default=[], help="Ordner, der wie das Gastsystem aufgebaut ist")
    ap.add_argument("--frameworks", action="append", default=[],
                    help="Ordner mit Frameworks, wie DYLD_FRAMEWORK_PATH im Gast")
    ap.add_argument("--arch", default="x86_64", choices=["x86_64", "arm64"], help="Teil des Mach-O (Standard x86_64)")
    ap.add_argument("--deep", action="store_true", help="auch die Importe der geladenen Bibliotheken prüfen")
    ap.add_argument("--alle", action="store_true", help="alle fehlenden Symbole zeigen, nicht nur 25 je Bibliothek")
    a = ap.parse_args()
    global ARCH
    ARCH = a.arch

    img = a.image or os.path.join(WORK, "download/pd-20.5/puredarwin.img")
    if not os.path.isfile(img):
        sys.exit("Image fehlt: " + img)
    cache = os.path.join(WORK, "sysroot", "root-" + os.path.basename(img).replace(".", "_"))
    guest = Guest(img, [os.path.abspath(o) for o in a.overlay], cache)

    prog = a.programm
    if prog.endswith(".app") or os.path.isdir(prog):
        info = plistlib.load(open(os.path.join(prog, "Contents/Info.plist"), "rb"))
        prog = os.path.join(prog, "Contents/MacOS", info["CFBundleExecutable"])
    if ARCH not in run("lipo", "-archs", prog).split():
        sys.exit(f"{prog}: kein {ARCH}-Teil (lipo -archs: {run('lipo', '-archs', prog).strip()})")

    main_img = Image(prog)
    exe_dir = os.path.dirname(os.path.abspath(prog))

    def locate(name, client):
        """Install-Name -> (Pfad auf dem Mac, Herkunft) wie dyld, ohne Shared Cache."""
        if name.startswith("@executable_path/"):
            p = os.path.normpath(os.path.join(exe_dir, name[17:]))
            return (p, "Bundle") if os.path.isfile(p) else (None, None)
        if name.startswith("@loader_path/"):
            p = os.path.normpath(os.path.join(os.path.dirname(client.path), name[13:]))
            return (p, "Bundle") if os.path.isfile(p) else (None, None)
        if name.startswith("@rpath/"):
            for rp in client.rpaths + main_img.rpaths:
                base = rp.replace("@executable_path", exe_dir).replace("@loader_path", os.path.dirname(client.path))
                p = os.path.normpath(os.path.join(base, name[7:]))
                if os.path.isfile(p):
                    return p, "Bundle"
            return None, None
        m = re.search(r"/([^/]+\.framework/.*)$", name)
        for fw in a.frameworks if m else []:   # wie DYLD_FRAMEWORK_PATH
            p = os.path.join(fw, m.group(1))
            if os.path.isfile(p):
                return p, "Frameworks"
        p = guest.resolve(name)
        return (p, "ClokeOS") if p else (None, None)

    loaded = {}      # install name -> Image
    missing = {}     # install name -> wer es lädt
    order = [("<Programm>", main_img)]
    queue = [main_img]
    while queue:
        cur = queue.pop(0)
        for name, _, weak in cur.deps:
            if name in loaded or name in missing:
                continue
            p, _ = locate(name, cur)
            if not p:
                if not weak:
                    missing[name] = cur.id or os.path.basename(cur.path)
                continue
            im = Image(p)
            loaded[name] = im
            order.append((name, im))
            queue.append(im)

    def all_exports(name, seen=None):
        seen = seen or set()
        if name in seen or name not in loaded:
            return set()
        seen.add(name)
        im = loaded[name]
        ex = set(im.exports())
        for dep, rex, _ in im.deps:
            if rex:
                ex |= all_exports(dep, seen)
        return ex

    cache_ex = {}

    def check(owner_name, im):
        byshort = {short_name(d): d for d, _, _ in im.deps}
        gaps = {}
        for sym, frm, weak in im.imports():
            if weak or frm in ("flat-namespace", "main-executable", "this-image", "weak-coalesce"):
                continue
            dep = byshort.get(frm)
            if not dep or dep in missing:
                continue
            if dep not in cache_ex:
                cache_ex[dep] = all_exports(dep)
            if sym not in cache_ex[dep]:
                gaps.setdefault(dep, []).append(sym)
        return gaps

    print(f"Programm: {prog}")
    print(f"Geladen: {len(loaded)} Bibliotheken"
          f" ({sum(1 for n in loaded if not n.startswith('@'))} aus ClokeOS, {sum(1 for n in loaded if n.startswith('@'))} aus dem Bundle)")
    if missing:
        print(f"\nFehlende Bibliotheken ({len(missing)}):")
        for n, by in sorted(missing.items()):
            print(f"  {n}   (gebraucht von {by})")
    targets = order if a.deep else order[:1]
    total = 0
    for owner, im in targets:
        gaps = check(owner, im)
        if not gaps:
            continue
        label = "Programm" if owner == "<Programm>" else owner
        print(f"\nFehlende Symbole in {label}:")
        for dep, syms in sorted(gaps.items()):
            total += len(syms)
            shown = sorted(syms) if a.alle else sorted(syms)[:25]
            more = "" if a.alle or len(syms) <= 25 else f" … (+{len(syms) - 25})"
            print(f"  {dep}: {len(syms)}\n    " + " ".join(shown) + more)
    if not missing and not total:
        print("\nAlles da: dyld findet alle Bibliotheken und Symbole.")
    else:
        print(f"\nErgebnis: {len(missing)} Bibliotheken und {total} Symbole fehlen.")
    sys.exit(1 if missing or total else 0)


if __name__ == "__main__":
    main()
