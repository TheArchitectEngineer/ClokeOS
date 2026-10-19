#!/usr/bin/env python3
"""Erzeugt aus dem Umhang-Logo der Oberfläche (LOGO_D in clokeos.html) einen C-Header für den Loader.

  logo.py <clokeos.html> <ausgabe.h>

Der SVG-Pfad (viewBox 0 0 64 64, fill-rule evenodd) wird in geschlossene Polygone zerlegt;
Bézierkurven und Bögen werden dabei in kurze Strecken aufgeteilt. Koordinaten stehen im Header
in 1/256 Logo-Einheiten, damit der Loader ohne Gleitkomma auskommt und das Logo in jeder
Bildschirmauflösung scharf zeichnen kann.
"""
import math
import re
import sys

STEPS = 16  # Strecken pro Kurvenstück


def logo_path(html):
    m = re.search(r"const LOGO_D = ((?:'[^']*'\s*\+?\s*)+);", html)
    if not m:
        sys.exit('LOGO_D nicht gefunden')
    return ''.join(re.findall(r"'([^']*)'", m.group(1)))


def tokens(d):
    for t in re.findall(r'[MmLlHhVvCcSsQqTtAaZz]|-?(?:\d+\.?\d*|\.\d+)(?:[eE][-+]?\d+)?', d):
        yield t


def arc_points(x0, y0, rx, ry, phi, large, sweep, x1, y1):
    # SVG-Implementierungshinweise F.6.5: Endpunkt- in Mittelpunktdarstellung umrechnen.
    phi = math.radians(phi)
    cp, sp = math.cos(phi), math.sin(phi)
    dx, dy = (x0 - x1) / 2, (y0 - y1) / 2
    x1p, y1p = cp * dx + sp * dy, -sp * dx + cp * dy
    rx, ry = abs(rx), abs(ry)
    lam = x1p ** 2 / rx ** 2 + y1p ** 2 / ry ** 2
    if lam > 1:
        rx, ry = rx * math.sqrt(lam), ry * math.sqrt(lam)
    num = rx ** 2 * ry ** 2 - rx ** 2 * y1p ** 2 - ry ** 2 * x1p ** 2
    den = rx ** 2 * y1p ** 2 + ry ** 2 * x1p ** 2
    co = math.sqrt(max(0.0, num / den)) * (-1 if large == sweep else 1)
    cxp, cyp = co * rx * y1p / ry, -co * ry * x1p / rx
    cx, cy = cp * cxp - sp * cyp + (x0 + x1) / 2, sp * cxp + cp * cyp + (y0 + y1) / 2

    def ang(ux, uy, vx, vy):
        a = math.atan2(ux * vy - uy * vx, ux * vx + uy * vy)
        return a
    t1 = ang(1, 0, (x1p - cxp) / rx, (y1p - cyp) / ry)
    dt = ang((x1p - cxp) / rx, (y1p - cyp) / ry, (-x1p - cxp) / rx, (-y1p - cyp) / ry)
    if not sweep and dt > 0:
        dt -= 2 * math.pi
    elif sweep and dt < 0:
        dt += 2 * math.pi
    n = max(4, int(abs(dt) / (math.pi / 16)))
    pts = []
    for i in range(1, n + 1):
        t = t1 + dt * i / n
        pts.append((cx + rx * math.cos(t) * cp - ry * math.sin(t) * sp,
                    cy + rx * math.cos(t) * sp + ry * math.sin(t) * cp))
    pts[-1] = (x1, y1)
    return pts


def polygons(d):
    polys, cur = [], []
    x = y = sx = sy = 0.0
    it = tokens(d)
    cmd = None
    pending = None

    def num():
        nonlocal pending
        if pending is not None:
            v, pending = pending, None
            return float(v)
        return float(next(it))

    for t in it:
        if re.match(r'[A-Za-z]', t):
            cmd = t
            if cmd in 'Zz':
                if cur:
                    polys.append(cur)
                cur = []
                x, y = sx, sy
            continue
        pending = t
        rel = cmd.islower()
        c = cmd.upper()
        if c == 'M':
            if cur:
                polys.append(cur)
            nx, ny = num(), num()
            x, y = (x + nx, y + ny) if rel else (nx, ny)
            sx, sy = x, y
            cur = [(x, y)]
            cmd = 'l' if rel else 'L'
        elif c == 'L':
            nx, ny = num(), num()
            x, y = (x + nx, y + ny) if rel else (nx, ny)
            cur.append((x, y))
        elif c == 'C':
            p = [num() for _ in range(6)]
            if rel:
                p = [p[i] + (x if i % 2 == 0 else y) for i in range(6)]
            x0, y0 = x, y
            for i in range(1, STEPS + 1):
                s = i / STEPS
                a, b, c2, e = (1 - s) ** 3, 3 * (1 - s) ** 2 * s, 3 * (1 - s) * s ** 2, s ** 3
                cur.append((a * x0 + b * p[0] + c2 * p[2] + e * p[4],
                            a * y0 + b * p[1] + c2 * p[3] + e * p[5]))
            x, y = p[4], p[5]
        elif c == 'A':
            rx, ry, phi, large, sweep, nx, ny = (num() for _ in range(7))
            if rel:
                nx, ny = x + nx, y + ny
            cur.extend(arc_points(x, y, rx, ry, phi, int(large), int(sweep), nx, ny))
            x, y = nx, ny
        else:
            sys.exit('Pfadbefehl %s wird nicht unterstützt' % cmd)
    if cur:
        polys.append(cur)
    return polys


def main():
    html = open(sys.argv[1], encoding='utf-8').read()
    polys = polygons(logo_path(html))
    out = ['/* Erzeugt von loader/logo.py aus LOGO_D in clokeos.html - nicht von Hand ändern. */',
           '/* Umhang-Logo, viewBox 0 0 64 64, fill-rule evenodd; Koordinaten in 1/256 Einheiten. */',
           '#define CLOKEOS_LOGO_UNITS 64', '',
           'static const INT16 clokeos_logo_pts[][2] = {']
    ends = []
    n = 0
    for poly in polys:
        if poly[0] == poly[-1]:
            poly = poly[:-1]
        line = []
        for px, py in poly:
            line.append('{%d,%d}' % (round(px * 256), round(py * 256)))
        for i in range(0, len(line), 8):
            out.append('  ' + ', '.join(line[i:i + 8]) + ',')
        n += len(poly)
        ends.append(n)
    out.append('};')
    out.append('')
    out.append('/* Ende (exklusiv) jedes geschlossenen Teilpfads in clokeos_logo_pts */')
    out.append('static const UINT16 clokeos_logo_ends[] = { %s };' % ', '.join(map(str, ends)))
    open(sys.argv[2], 'w').write('\n'.join(out) + '\n')
    print('%s: %d Teilpfade, %d Punkte' % (sys.argv[2], len(ends), n))


if __name__ == '__main__':
    main()
