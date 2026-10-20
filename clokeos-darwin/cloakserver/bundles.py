#!/usr/bin/env python3
"""Legt die Programme von ClokeOS als .app-Bundles an, aufgebaut wie bei macOS:

  Name.app/Contents/Info.plist          CFBundleName, CFBundleIdentifier, CFBundleExecutable, …
  Name.app/Contents/MacOS/<Startdatei>  hier ein Shell-Skript, das das eigentliche Programm startet
  Name.app/Contents/Resources/AppIcon.svg

Systemprogramme liegen wie bei macOS in /System/Applications (Dienstprogramme in Utilities/),
Fundus wie der Finder in /System/Library/CoreServices, nachinstallierte Programme in /Applications.
Dazu kommt die Grundeinstellung des Docks in /Library/Preferences/com.clokeos.dock.plist.

  bundles.py <stage> <icons-ordner> <version> <build>

Zwei Schlüssel kennt nur CloakServer: CLWindowClasses (WM_CLASS der Fenster, * = beliebiger Teil),
damit Fenster zum Programm im Dock finden, und CLInternal für Programme, die CloakServer selbst
zeichnet (das Launchpad „Programme“).
"""
import colorsys
import os
import sys
from xml.sax.saxutils import escape


def term(ident, title, size, cmd):
    """Programm, das im Terminal läuft; die Fensterrolle ist die Bundle-Kennung."""
    return (f"exec xfce4-terminal --disable-server --role={ident} --title='{title}' "
            f"--dynamic-title-mode=none --geometry={size} -x {cmd}")


# Ordner, Name, Kennung, Icon aus clokeos.html (oder None), Startzeile, Fensterklassen, intern
APPS = [
    ('System/Library/CoreServices', 'Fundus', 'com.clokeos.fundus', 'dateien', 'exec thunar "$@"', 'Thunar', ''),
    ('System/Applications', 'Programme', 'com.clokeos.launchpad', 'programme', None, '', 'launchpad'),
    ('System/Applications', 'Search', 'com.clokeos.search', 'search', 'exec netsurf-gtk3 "$@"',
     'Netsurf-gtk3 Netsurf* NetSurf*', ''),
    ('System/Applications/Utilities', 'Terminal', 'com.clokeos.terminal', 'terminal', 'exec xfce4-terminal "$@"',
     'Xfce4-terminal XTerm UXTerm', ''),
    ('System/Applications', 'Notizen', 'com.clokeos.notizen', 'notizen',
     term('com.clokeos.notizen', 'Notizen', '84x28', 'nano "$HOME/Notizen.txt"'), '', ''),
    ('System/Applications', 'Rechner', 'com.clokeos.rechner', 'rechner', 'exec xcalc', 'XCalc', ''),
    ('System/Applications', 'Kalender', 'com.clokeos.kalender', 'kalender',
     term('com.clokeos.kalender', 'Kalender', '72x22', '/usr/libexec/clokeos-kalender'), '', ''),
    ('System/Applications/Utilities', 'Aktivitätsanzeige', 'com.clokeos.aktivitaet', 'aktivitaet',
     term('com.clokeos.aktivitaet', 'Aktivitätsanzeige', '92x30', '/usr/libexec/clokeos-aktivitaet'), '', ''),
    ('System/Applications', 'Systemeinstellungen', 'com.clokeos.systemeinstellungen', 'einstellungen', None, '',
     'settings'),
    ('System/Applications/Utilities', 'Xfce-Einstellungen', 'org.xfce.settings.manager', None,
     'exec xfce4-settings-manager', 'Xfce4-settings* Xfce4-*-settings Xfwm4-settings', ''),
    # nachinstalliert wie ein Programm aus dem Netz: liegt in /Applications
    ('Applications', 'Dillo', 'org.dillo.dillo', None, 'exec dillo "$@"', 'Dillo', ''),
]

DOCK = ['com.clokeos.fundus', 'com.clokeos.launchpad', 'com.clokeos.search', 'com.clokeos.terminal',
        'com.clokeos.notizen', 'com.clokeos.rechner', 'com.clokeos.kalender', 'com.clokeos.aktivitaet',
        'com.clokeos.systemeinstellungen']

HEAD = ('<?xml version="1.0" encoding="UTF-8"?>\n'
        '<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">\n'
        '<plist version="1.0">\n<dict>\n')


def plist(items):
    out = [HEAD]
    for k, v in items:
        out.append(f'\t<key>{escape(k)}</key>\n')
        if isinstance(v, list):
            out.append('\t<array>\n' + ''.join(f'\t\t<string>{escape(s)}</string>\n' for s in v) + '\t</array>\n')
        else:
            out.append(f'\t<string>{escape(v)}</string>\n')
    out.append('</dict>\n</plist>\n')
    return ''.join(out)


def hsl(h, s, l):
    """librsvg 2.40 im Image kennt keine hsl()-Farben"""
    r, g, b = colorsys.hls_to_rgb(h / 360, l / 100, s / 100)
    return '#%02x%02x%02x' % (round(r * 255), round(g * 255), round(b * 255))


def generic_icon(name, hue):
    """Icon für Programme ohne eigenes: abgerundetes Quadrat mit Anfangsbuchstaben."""
    return f'''<svg xmlns="http://www.w3.org/2000/svg" width="64" height="64" viewBox="0 0 64 64">
<defs><linearGradient id="g" x1="0" y1="0" x2="0" y2="1">
<stop offset="0" stop-color="{hsl(hue, 70, 62)}"/><stop offset="1" stop-color="{hsl(hue, 65, 42)}"/></linearGradient></defs>
<rect x="4" y="4" width="56" height="56" rx="13" fill="url(#g)"/>
<rect x="4.5" y="4.5" width="55" height="55" rx="12.5" fill="none" stroke="#fff" stroke-opacity=".25"/>
<text x="32" y="43" font-family="DejaVu Sans" font-weight="bold" font-size="30" fill="#fff" text-anchor="middle">{escape(name[0])}</text>
</svg>
'''


def write(path, text, mode=0o644):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, 'w', encoding='utf-8') as f:
        f.write(text)
    os.chmod(path, mode)


def ascii_name(name):
    """Ordner- und Dateinamen ohne Umlaute: Der Anzeigename steht in der Info.plist (wie bei macOS,
    wo „Aktivitätsanzeige“ im Ordner Activity Monitor.app liegt), und tar oder debugfs müssen
    keine Zeichensätze umwandeln."""
    for a, b in (('ä', 'ae'), ('ö', 'oe'), ('ü', 'ue'), ('Ä', 'Ae'), ('Ö', 'Oe'), ('Ü', 'Ue'), ('ß', 'ss')):
        name = name.replace(a, b)
    assert name.isascii() and ' ' not in name, name
    return name


def main(stage, icons, version, build):
    for folder, name, ident, icon, start, classes, internal in APPS:
        exe = ascii_name(name)
        app = os.path.join(stage, folder, exe + '.app', 'Contents')
        items = [('CFBundleDevelopmentRegion', 'de'), ('CFBundleDisplayName', name), ('CFBundleExecutable', exe),
                 ('CFBundleIconFile', 'AppIcon'), ('CFBundleIdentifier', ident),
                 ('CFBundleInfoDictionaryVersion', '6.0'), ('CFBundleName', name), ('CFBundlePackageType', 'APPL'),
                 ('CFBundleShortVersionString', version), ('CFBundleVersion', build)]
        if classes:
            items.append(('CLWindowClasses', classes))
        if internal:
            items.append(('CLInternal', internal))
        write(os.path.join(app, 'Info.plist'), plist(items))
        write(os.path.join(app, 'PkgInfo'), 'APPL????')
        script = '#!/bin/sh\n'
        script += start + '\n' if start else f'# {name} zeichnet CloakServer selbst ({internal}).\nexit 0\n'
        write(os.path.join(app, 'MacOS', exe), script, 0o755)
        if icon:
            with open(os.path.join(icons, icon + '.svg'), encoding='utf-8') as f:
                svg = f.read()
        else:
            svg = generic_icon(name, sum(map(ord, name)) * 37 % 360)
        write(os.path.join(app, 'Resources', 'AppIcon.svg'), svg)
    write(os.path.join(stage, 'Library/Preferences/com.clokeos.dock.plist'), plist([('persistent-apps', DOCK)]))
    print('Bundles: %d Programme, Dock mit %d' % (len(APPS), len(DOCK)))


if __name__ == '__main__':
    if len(sys.argv) != 5:
        sys.exit(__doc__)
    main(*sys.argv[1:])
