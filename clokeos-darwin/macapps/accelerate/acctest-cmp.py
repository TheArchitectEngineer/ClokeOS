#!/usr/bin/env python3
"""Vergleicht zwei acctest-dump-Ausgaben (z. B. Apples Accelerate gegen unsere Bibliothek): acctest-cmp.py A B [Toleranz]"""
import sys
a = {l.split()[0]: l.split()[1:] for l in open(sys.argv[1]) if l.strip()}
b = {l.split()[0]: l.split()[1:] for l in open(sys.argv[2]) if l.strip()}
tol = float(sys.argv[3]) if len(sys.argv) > 3 else 2e-5
bad = 0
for k in a:
    if k not in b: print("fehlt:", k); bad += 1; continue
    x = [float(v) for v in a[k]]; y = [float(v) for v in b[k]]
    sc = max([1.0] + [abs(v) for v in x])
    t = 1e-4 if k.startswith("fft") or k.startswith("vq") else tol
    if k.startswith("dgeev"): t = 1e-6
    e = max((abs(p - q) for p, q in zip(x, y)), default=0)
    if len(x) != len(y) or e > t * sc or e != e:
        print("ABWEICHUNG %s: max Fehler %.3g (Skala %.3g)" % (k, e, sc)); bad += 1
print("%d von %d Gruppen verschieden" % (bad, len(a)))
sys.exit(1 if bad else 0)
