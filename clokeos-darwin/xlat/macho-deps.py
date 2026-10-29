#!/usr/bin/env python3
"""Inventar eines Mac-Programms: alle Mach-O-Dateien, ihre Abhängigkeiten und importierten Symbole.

    xlat/macho-deps.py APP_VERZEICHNIS [--provider NAME=DATEI ...] [--json AUS.json]

Liest für jede Mach-O-Datei im Verzeichnis (x86_64-Slice) die Architekturen, `otool -L` und die
Importe (`dyld_info -imports`), zählt die Importe je Bibliothek und – mit --provider NAME=DATEI
(z. B. Foundation=…/Foundation.framework/Foundation) – wie viele davon die Datei exportiert.
Schreibt nichts in das Verzeichnis. Benutzt lipo, otool und dyld_info aus Xcode/Command Line Tools.
"""
import argparse
import collections
import concurrent.futures
import json
import os
import re
import subprocess
import sys

MAGIC = {b"\xcf\xfa\xed\xfe", b"\xca\xfe\xba\xbe", b"\xbe\xba\xfe\xca"}


def run(*cmd):
    return subprocess.run(cmd, capture_output=True, text=True, errors="replace").stdout


def macho_files(root):
    for d, _, files in os.walk(root):
        for n in files:
            p = os.path.join(d, n)
            if os.path.islink(p) or not os.path.isfile(p):
                continue
            try:
                with open(p, "rb") as f:
                    if f.read(4) in MAGIC:
                        yield p
            except OSError:
                pass


def inspect(path):
    archs = run("lipo", "-archs", path).split()
    arch = "x86_64" if "x86_64" in archs else (archs[0] if archs else "")
    deps = []
    for line in run("otool", "-arch", arch, "-L", path).splitlines()[1:]:
        m = re.match(r"\s+(\S+) \(", line)
        if m:
            deps.append(m.group(1))
    imports = collections.Counter()
    syms = collections.defaultdict(set)
    for line in run("dyld_info", "-arch", arch, "-imports", path).splitlines():
        m = re.match(r"\s+0x[0-9A-F]+\s+(\S+)\s+\(from (.+)\)", line)
        if m:
            imports[m.group(2)] += 1
            syms[m.group(2)].add(m.group(1))
    return {"path": path, "archs": archs, "deps": deps, "imports": dict(imports),
            "syms": {k: sorted(v) for k, v in syms.items()}}


def exports(path):
    out = set()
    for line in run("dyld_info", "-arch", "x86_64", "-exports", path).splitlines():
        m = re.match(r"\s+0x[0-9A-F]+\s+(\S+)", line)
        if m:
            out.add(m.group(1))
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("root")
    ap.add_argument("--provider", action="append", default=[])
    ap.add_argument("--json")
    a = ap.parse_args()
    files = sorted(macho_files(a.root))
    with concurrent.futures.ThreadPoolExecutor(8) as ex:
        info = list(ex.map(inspect, files))
    for i in info:
        i["path"] = os.path.relpath(i["path"], a.root)
    prov = {}
    for p in a.provider:
        name, _, f = p.partition("=")
        prov.setdefault(name, set()).update(exports(f))
    total = collections.defaultdict(set)
    for i in info:
        for lib, s in i["syms"].items():
            total[lib].update(s)
    res = {"files": info, "libs": {}}
    for lib, s in sorted(total.items(), key=lambda kv: -len(kv[1])):
        e = prov.get(lib)
        res["libs"][lib] = {"unique_imports": len(s),
                            "provided": None if e is None else len(s & e),
                            "missing_sample": [] if e is None else sorted(s - e)[:5]}
    if a.json:
        with open(a.json, "w") as f:
            json.dump(res, f, indent=1)
    print(f"{len(info)} Mach-O-Dateien")
    for lib, v in res["libs"].items():
        pv = "-" if v["provided"] is None else v["provided"]
        print(f"{v['unique_imports']:6} {pv!s:>6}  {lib}")


if __name__ == "__main__":
    sys.exit(main())
