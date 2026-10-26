#!/usr/bin/env python3
"""Erzeugt die symbolischen GTK-Symbole im Mac-Stil (dünne Linien wie SF Symbols) für das Symbolthema ClokeOS.

GTK färbt symbolische Symbole nur über fill, deshalb ist alles Fläche: Linien sind Vierecke mit runden Enden,
Ringe und Rahmen Pfade mit Loch (evenodd). Aufruf: tools/gen-symbolic-icons.py (schreibt nach
overlay-desktop/share/icons/ClokeOS/scalable/actions, je Symbol -symbolic und ohne Endung)."""
import math
import os

OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'overlay-desktop/share/icons/ClokeOS/scalable/actions')
W = 1.4  # Strichstärke


def f(v):
    return ('%.2f' % v).rstrip('0').rstrip('.')


def seg(x1, y1, x2, y2, w=W):
    dx, dy = x2 - x1, y2 - y1
    n = math.hypot(dx, dy) or 1
    nx, ny = -dy / n * w / 2, dx / n * w / 2
    d = 'M%s %sL%s %sL%s %sL%s %sz' % (f(x1 + nx), f(y1 + ny), f(x2 + nx), f(y2 + ny), f(x2 - nx), f(y2 - ny), f(x1 - nx), f(y1 - ny))
    return '<path d="%s"/><circle cx="%s" cy="%s" r="%s"/><circle cx="%s" cy="%s" r="%s"/>' % (d, f(x1), f(y1), f(w / 2), f(x2), f(y2), f(w / 2))


def poly(pts, w=W, close=False):
    out = ''
    n = len(pts)
    for i in range(n - (0 if close else 1)):
        a, b = pts[i], pts[(i + 1) % n]
        out += seg(a[0], a[1], b[0], b[1], w)
    return out


def circ(cx, cy, r):
    return '<circle cx="%s" cy="%s" r="%s"/>' % (f(cx), f(cy), f(r))


def ring(cx, cy, r, w=W):
    ro, ri = r + w / 2, r - w / 2
    return '<path fill-rule="evenodd" d="M%s %sa%s %s 0 1 0 %s 0a%s %s 0 1 0 %s 0zM%s %sa%s %s 0 1 0 %s 0a%s %s 0 1 0 %s 0z"/>' % (
        f(cx - ro), f(cy), f(ro), f(ro), f(2 * ro), f(ro), f(ro), f(-2 * ro), f(cx - ri), f(cy), f(ri), f(ri), f(2 * ri), f(ri), f(ri), f(-2 * ri))


def rr(x, y, w, h, r):
    r = min(r, w / 2, h / 2)
    return 'M%s %sh%sa%s %s 0 0 1 %s %sv%sa%s %s 0 0 1 %s %sh%sa%s %s 0 0 1 %s %sv%sa%s %s 0 0 1 %s %sz' % (
        f(x + r), f(y), f(w - 2 * r), f(r), f(r), f(r), f(r), f(h - 2 * r), f(r), f(r), f(-r), f(r), f(-(w - 2 * r)), f(r), f(r), f(-r), f(-r), f(-(h - 2 * r)), f(r), f(r), f(r), f(-r))


def box(x, y, w, h, r=1.5, sw=W):
    return '<path fill-rule="evenodd" d="%s%s"/>' % (rr(x - sw / 2, y - sw / 2, w + sw, h + sw, r + sw / 2), rr(x + sw / 2, y + sw / 2, w - sw, h - sw, max(r - sw / 2, 0)))


def fillbox(x, y, w, h, r=1):
    return '<path d="%s"/>' % rr(x, y, w, h, r)


def arc(cx, cy, r, a0, a1, w=W):
    pts = []
    steps = max(4, int(abs(a1 - a0) / 15))
    for i in range(steps + 1):
        a = math.radians(a0 + (a1 - a0) * i / steps)
        pts.append((cx + r * math.cos(a), cy + r * math.sin(a)))
    return poly(pts, w)


def dot(x, y, r=.8):
    return circ(x, y, r)


def star(cx, cy, ro, ri):
    pts = []
    for i in range(10):
        a = math.radians(-90 + i * 36)
        r = ro if i % 2 == 0 else ri
        pts.append((cx + r * math.cos(a), cy + r * math.sin(a)))
    return '<path d="M%sz"/>' % 'L'.join('%s %s' % (f(x), f(y)) for x, y in pts)


def tri(pts):
    return '<path d="M%sz"/>' % 'L'.join('%s %s' % (f(x), f(y)) for x, y in pts)


def folder(inner=''):
    body = '<path fill-rule="evenodd" d="M1.5 3.8Q1.5 2.8 2.5 2.8H6l1.4 1.5h6.1q1 0 1 1V12q0 1-1 1H2.5q-1 0-1-1zM2.9 5.7V11.6H13.1V5.7z"/>'
    return body + inner


ICONS = {
    'window-minimize': seg(3, 8, 13, 8),
    'window-restore': box(2.5, 5, 8, 8, 1.4) + poly([(5.5, 3.3), (5.5, 2.5), (13.5, 2.5), (13.5, 10.5), (12.2, 10.5)], W),
    'window-new': box(2, 3, 12, 10, 1.6) + seg(8, 6, 8, 10) + seg(6, 8, 10, 8),
    'tab-new': seg(8, 3, 8, 13) + seg(3, 8, 13, 8),
    'go-first': seg(3.5, 3.5, 3.5, 12.5) + poly([(11.5, 3.5), (6.5, 8), (11.5, 12.5)]),
    'go-last': seg(12.5, 3.5, 12.5, 12.5) + poly([(4.5, 3.5), (9.5, 8), (4.5, 12.5)]),
    'view-fullscreen': poly([(2.5, 6), (2.5, 2.5), (6, 2.5)]) + poly([(10, 2.5), (13.5, 2.5), (13.5, 6)]) + poly([(13.5, 10), (13.5, 13.5), (10, 13.5)]) + poly([(6, 13.5), (2.5, 13.5), (2.5, 10)]),
    'view-restore': poly([(2.5, 6.5), (6.5, 6.5), (6.5, 2.5)]) + poly([(9.5, 2.5), (9.5, 6.5), (13.5, 6.5)]) + poly([(13.5, 9.5), (9.5, 9.5), (9.5, 13.5)]) + poly([(6.5, 13.5), (6.5, 9.5), (2.5, 9.5)]),
    'view-more': dot(3.5, 8, 1.1) + dot(8, 8, 1.1) + dot(12.5, 8, 1.1),
    'view-more-horizontal': dot(3.5, 8, 1.1) + dot(8, 8, 1.1) + dot(12.5, 8, 1.1),
    'zoom-in': ring(7, 7, 4.3) + seg(10.2, 10.2, 13.5, 13.5) + seg(7, 5.2, 7, 8.8) + seg(5.2, 7, 8.8, 7),
    'zoom-out': ring(7, 7, 4.3) + seg(10.2, 10.2, 13.5, 13.5) + seg(5.2, 7, 8.8, 7),
    'zoom-fit-best': ring(7, 7, 4.3) + seg(10.2, 10.2, 13.5, 13.5) + seg(5.4, 6, 8.6, 6) + seg(5.4, 8, 8.6, 8),
    'zoom-original': ring(7, 7, 4.3) + seg(10.2, 10.2, 13.5, 13.5) + seg(7, 5.4, 7, 8.6),
    'edit-undo': poly([(5.5, 3.5), (3, 6), (5.5, 8.5)]) + seg(3.2, 6, 9.5, 6) + arc(9.5, 9.2, 3.2, -90, 90),
    'edit-redo': poly([(10.5, 3.5), (13, 6), (10.5, 8.5)]) + seg(12.8, 6, 6.5, 6) + arc(6.5, 9.2, 3.2, -90, -270),
    'edit-select-all': box(2, 2, 12, 12, 2) + poly([(5, 8.2), (7.2, 10.4), (11, 5.8)]),
    'document-save-as': box(3, 1.8, 10, 12.4, 1.4) + seg(5.5, 6, 10.5, 6) + seg(5.5, 8.5, 8.5, 8.5),
    'document-print': box(3, 6, 10, 5, 1.3) + poly([(5, 6), (5, 2.5), (11, 2.5), (11, 6)]) + box(5, 9.5, 6, 4, .5),
    'document-revert': arc(8, 8, 4.8, 200, -70) + poly([(11, 1.8), (11.6, 5), (8.4, 5.3)]),
    'document-send': poly([(8, 10), (8, 2.5)]) + poly([(5.2, 5), (8, 2.2), (10.8, 5)]) + poly([(3.5, 8.5), (3.5, 13), (12.5, 13), (12.5, 8.5)]),
    'folder': folder(),
    'folder-open': '<path fill-rule="evenodd" d="M1.5 3.8Q1.5 2.8 2.5 2.8H6l1.4 1.5H12.5q1 0 1 1v1.2H4.4Q3.6 6.5 3.4 7.2L1.5 12.3zM4.1 7.9H15L13 12.4Q12.8 13 12 13H2.5Q2 13 2.3 12.5z"/>',
    'folder-documents': folder(seg(5.5, 8, 10.5, 8) + seg(5.5, 10, 9, 10)),
    'folder-download': folder(seg(8, 7, 8, 11) + poly([(6.2, 9.3), (8, 11.1), (9.8, 9.3)])),
    'folder-music': folder(circ(7.4, 10, 1.2) + seg(8.6, 10, 8.6, 6.8)),
    'folder-pictures': folder(poly([(4.5, 11), (7, 8.3), (9, 10), (10.2, 9)], 1.2)),
    'folder-videos': folder(tri([(7, 7.2), (7, 10.8), (10, 9)])),
    'folder-publicshare': folder(ring(8, 9.2, 1.9, 1.1)),
    'folder-templates': folder(box(5.5, 7, 5, 3.5, .6, 1.1)),
    'user-home': poly([(2, 8), (8, 2.5), (14, 8)]) + poly([(3.8, 7), (3.8, 13.3), (12.2, 13.3), (12.2, 7)]) + poly([(6.7, 13.3), (6.7, 9.5), (9.3, 9.5), (9.3, 13.3)]),
    'user-desktop': box(1.8, 2.5, 12.4, 8.5, 1.5) + seg(5.5, 13.5, 10.5, 13.5) + seg(8, 11, 8, 13.5),
    'user-bookmarks': poly([(4, 2.5), (12, 2.5), (12, 13.5), (8, 10.5), (4, 13.5)], W, True),
    'bookmark-new': poly([(4, 2.5), (12, 2.5), (12, 13.5), (8, 10.5), (4, 13.5)], W, True) + seg(8, 4.8, 8, 8.2) + seg(6.3, 6.5, 9.7, 6.5),
    'starred': star(8, 8.3, 6.4, 2.7),
    'non-starred': '<path fill-rule="evenodd" d="%s"/>' % ('M%sz' % 'L'.join('%s %s' % (f(8 + (6.9 if i % 2 == 0 else 3.1) * math.cos(math.radians(-90 + i * 36))), f(8.3 + (6.9 if i % 2 == 0 else 3.1) * math.sin(math.radians(-90 + i * 36)))) for i in range(10)) + 'M%sz' % 'L'.join('%s %s' % (f(8 + (5.0 if i % 2 == 0 else 2.2) * math.cos(math.radians(-90 + i * 36))), f(8.3 + (5.0 if i % 2 == 0 else 2.2) * math.sin(math.radians(-90 + i * 36)))) for i in range(10))),
    'media-playback-pause': fillbox(4, 3, 3, 10, 1) + fillbox(9, 3, 3, 10, 1),
    'media-playback-stop': fillbox(3.5, 3.5, 9, 9, 1.4),
    'media-skip-forward': tri([(3, 3.5), (3, 12.5), (10, 8)]) + fillbox(11, 3.5, 2, 9, .8),
    'media-skip-backward': tri([(13, 3.5), (13, 12.5), (6, 8)]) + fillbox(3, 3.5, 2, 9, .8),
    'media-seek-forward': tri([(2, 4), (2, 12), (8, 8)]) + tri([(8, 4), (8, 12), (14, 8)]),
    'media-seek-backward': tri([(14, 4), (14, 12), (8, 8)]) + tri([(8, 4), (8, 12), (2, 8)]),
    'media-record': circ(8, 8, 4.5),
    'audio-volume-high': tri([(2.5, 6), (5, 6), (8.5, 3), (8.5, 13), (5, 10), (2.5, 10)]) + arc(8.5, 8, 3.2, -45, 45) + arc(8.5, 8, 5.6, -45, 45),
    'audio-volume-medium': tri([(2.5, 6), (5, 6), (8.5, 3), (8.5, 13), (5, 10), (2.5, 10)]) + arc(8.5, 8, 3.4, -45, 45),
    'audio-volume-low': tri([(3, 6), (5.5, 6), (9, 3), (9, 13), (5.5, 10), (3, 10)]),
    'audio-volume-muted': tri([(2.5, 6), (5, 6), (8.5, 3), (8.5, 13), (5, 10), (2.5, 10)]) + seg(10.5, 6, 14, 10) + seg(14, 6, 10.5, 10),
    'network-wireless': arc(8, 12, 3.5, -135, -45) + arc(8, 12, 6.5, -135, -45) + arc(8, 12, 9.5, -135, -45) + dot(8, 12, 1.1),
    'network-wired': box(3.5, 2.5, 9, 6, 1.2) + seg(8, 8.5, 8, 11.5) + seg(3, 11.5, 13, 11.5) + seg(3, 11.5, 3, 13.5) + seg(13, 11.5, 13, 13.5),
    'network-offline': arc(8, 12, 3.5, -135, -45) + arc(8, 12, 6.5, -135, -45) + dot(8, 12, 1.1) + seg(2.5, 2.5, 13.5, 13.5),
    'system-shutdown': arc(8, 8.5, 5, -60, 240) + seg(8, 2, 8, 7.5),
    'system-reboot': arc(8, 8, 5, 30, 330) + poly([(10, 1.5), (13, 4.2), (9.2, 5.5)]),
    'system-lock-screen': box(3.5, 7, 9, 6.5, 1.5) + poly([(5.5, 7), (5.5, 5), (6.3, 3.2), (8, 2.6), (9.7, 3.2), (10.5, 5), (10.5, 7)]),
    'system-log-out': poly([(7, 3), (3, 3), (3, 13), (7, 13)]) + seg(6.5, 8, 13, 8) + poly([(10.5, 5.5), (13, 8), (10.5, 10.5)]),
    'dialog-password': box(3.5, 7, 9, 6.5, 1.5) + poly([(5.5, 7), (5.5, 5), (6.3, 3.2), (8, 2.6), (9.7, 3.2), (10.5, 5), (10.5, 7)]) + dot(8, 10.2, .9),
    'dialog-question': ring(8, 8, 6) + poly([(6, 6.2), (6.4, 5), (8, 4.5), (9.6, 5.1), (9.8, 6.4), (8, 8), (8, 9)]) + dot(8, 11, .85),
    'help-about': ring(8, 8, 6) + seg(8, 7.2, 8, 11.3) + dot(8, 4.9, .85),
    'help-browser': ring(8, 8, 6) + poly([(6, 6.2), (6.4, 5), (8, 4.5), (9.6, 5.1), (9.8, 6.4), (8, 8), (8, 9)]) + dot(8, 11, .85),
    'mail-send': poly([(2, 2.5), (14, 8), (2, 13.5), (4, 8)], W, True) + seg(4, 8, 9, 8),
    'mail-unread': box(2, 3.5, 12, 9, 1.6) + poly([(2.8, 4.8), (8, 8.8), (13.2, 4.8)]),
    'image-x-generic': box(1.8, 2.5, 12.4, 11, 1.6) + poly([(2.4, 11.5), (6, 7.5), (9, 10.5), (10.8, 8.8), (13.6, 11.8)]) + circ(10.7, 5.7, 1.1),
    'audio-x-generic': circ(5, 11.5, 2) + seg(7, 11.5, 7, 3.5) + poly([(7, 3.5), (12.5, 2.5), (12.5, 6), (7, 7)]),
    'video-x-generic': box(1.8, 3, 12.4, 10, 1.6) + tri([(6.7, 6), (6.7, 10), (10, 8)]),
    'application-x-executable': box(1.8, 2.5, 12.4, 11, 1.8) + seg(1.8, 5.5, 14.2, 5.5) + dot(3.8, 4, .6) + dot(5.6, 4, .6),
    'package-x-generic': poly([(8, 2), (13.5, 4.5), (13.5, 11.5), (8, 14), (2.5, 11.5), (2.5, 4.5)], W, True) + poly([(2.5, 4.5), (8, 7), (13.5, 4.5)]) + seg(8, 7, 8, 14),
    'text-x-script': box(2, 3, 12, 10, 1.6) + poly([(5, 6), (7, 8), (5, 10)]) + seg(8.5, 10, 11.2, 10),
    'utilities-terminal': box(1.8, 2.8, 12.4, 10.4, 1.8) + poly([(4.8, 6), (7, 8), (4.8, 10)]) + seg(8.5, 10, 11.4, 10),
    'x-office-calendar': box(2, 3, 12, 10.5, 1.6) + seg(2, 6.5, 14, 6.5) + seg(5.2, 1.8, 5.2, 4) + seg(10.8, 1.8, 10.8, 4),
    'appointment-new': box(2, 3, 12, 10.5, 1.6) + seg(2, 6.5, 14, 6.5) + seg(8, 8, 8, 12) + seg(6, 10, 10, 10),
    'avatar-default': circ(8, 5.5, 2.8) + '<path d="M2.5 14Q2.5 9.2 8 9.2T13.5 14z"/>',
    'edit-find-replace': ring(6.5, 6.5, 4) + seg(9.5, 9.5, 13.5, 13.5) + seg(5, 6.5, 8, 6.5),
    'insert-image': box(1.8, 2.5, 12.4, 11, 1.6) + poly([(2.4, 11.5), (6, 7.5), (9, 10.5), (10.8, 8.8), (13.6, 11.8)]),
    'object-rotate-left': arc(8, 8.5, 4.8, 140, 400) + poly([(2.5, 4), (3, 7.5), (6.3, 6.5)]),
    'object-rotate-right': arc(8, 8.5, 4.8, 40, -220) + poly([(13.5, 4), (13, 7.5), (9.7, 6.5)]),
    'object-flip-horizontal': seg(8, 2, 8, 14) + tri([(6.5, 4), (6.5, 12), (2, 12)]) + poly([(9.5, 4), (14, 12), (9.5, 12)], 1.2, True),
    'process-working': arc(8, 8, 5, 20, 320) + poly([(9.5, 1.8), (12, 3.2), (9.8, 4.8)]),
    'edit-paste-symbolic-placeholder': '',
    'document-edit': poly([(10.5, 2.5), (13.5, 5.5), (6, 13), (3, 13), (3, 10)], W, True) + seg(9, 4, 12, 7),
    'format-text-bold': '<path d="M5 3h3.4a2.6 2.6 0 0 1 1.5 4.7A2.9 2.9 0 0 1 8.9 13H5zm1.6 1.5v2.7h1.7a1.3 1.3 0 0 0 0-2.7zm0 4.2v2.8h2.1a1.4 1.4 0 0 0 0-2.8z" fill-rule="evenodd"/>',
    'format-text-italic': seg(9.5, 3, 6, 13) + seg(7.5, 3, 11.5, 3) + seg(4.5, 13, 8.5, 13),
    'format-text-underline': arc(8, 6.5, 3.5, 180, 0) + seg(4.5, 3, 4.5, 6.5) + seg(11.5, 3, 11.5, 6.5) + seg(3.5, 13.2, 12.5, 13.2),
    'format-justify-left': seg(2.5, 3.5, 13.5, 3.5) + seg(2.5, 6.5, 10, 6.5) + seg(2.5, 9.5, 13.5, 9.5) + seg(2.5, 12.5, 9, 12.5),
    'format-justify-center': seg(2.5, 3.5, 13.5, 3.5) + seg(4.5, 6.5, 11.5, 6.5) + seg(2.5, 9.5, 13.5, 9.5) + seg(5, 12.5, 11, 12.5),
    'format-justify-right': seg(2.5, 3.5, 13.5, 3.5) + seg(6, 6.5, 13.5, 6.5) + seg(2.5, 9.5, 13.5, 9.5) + seg(7, 12.5, 13.5, 12.5),
    'view-sidebar': box(1.8, 2.8, 12.4, 10.4, 1.8) + seg(6, 3, 6, 13),
    'go-bottom': seg(8, 3, 8, 10.5) + poly([(4.5, 7.5), (8, 11), (11.5, 7.5)]) + seg(3.5, 13.2, 12.5, 13.2),
    'insert-link': arc(5.2, 8, 2.8, 90, 270) + arc(10.8, 8, 2.8, -90, 90) + seg(5.2, 5.2, 10.8, 5.2) + seg(5.2, 10.8, 10.8, 10.8),
    'emblem-shared': ring(5, 8, 1.8, 1.1) + ring(11.5, 4.5, 1.8, 1.1) + ring(11.5, 11.5, 1.8, 1.1) + seg(6.5, 7.2, 10, 5.2) + seg(6.5, 8.8, 10, 10.8),
}
ICONS.pop('edit-paste-symbolic-placeholder')
SVG = '<svg xmlns="http://www.w3.org/2000/svg" width="16" height="16" viewBox="0 0 16 16"><g fill="#2e3436">%s</g></svg>'

n = 0
for name, body in ICONS.items():
    svg = SVG % body
    for fn in (name + '-symbolic.svg', name + '.svg'):
        p = os.path.join(OUT, fn)
        if os.path.exists(p) and fn in KEEP if False else False:
            continue
        open(p, 'w').write(svg)
        n += 1
print(n, 'Dateien,', len(ICONS), 'Symbole')
