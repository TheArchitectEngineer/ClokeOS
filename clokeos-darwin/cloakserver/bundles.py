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


def term(ident, title_en, title_de, size, cmd):
    """Programm, das im Terminal läuft; die Fensterrolle ist die Bundle-Kennung. Der Fenstertitel
    folgt der Sprache von ClokeOS (/etc/clokeos-language)."""
    return (f"title='{title_en}'; [ \"$(cat /etc/clokeos-language 2>/dev/null)\" = de ] && title='{title_de}'\n"
            f"exec xfce4-terminal --disable-server --role={ident} --title=\"$title\" "
            f"--dynamic-title-mode=none --geometry={size} -x {cmd}")


# Search ist NetSurf mit der Startseite aus /share/clokeos (nur beim ersten Start, danach gilt, was NetSurf in
# Choices speichert; die mitgelieferte Willkommensseite von NetSurf wird ersetzt)
SEARCH_START = (
    "l=$(cut -c1-2 /etc/clokeos-language 2>/dev/null); case \"$l\" in de|fr|es|it) ;; *) l=en;; esac; t=hell; [ \"$CLOKEOS_DARK\" = 1 ] && t=dunkel\n"
    "start=/share/clokeos/search-$l-$t.html\n"
    "c=\"$HOME/.netsurf/Choices\"; mkdir -p \"$HOME/.netsurf\"\n"
    "{ grep -v -e '^homepage_url:' -e '^toolbar_items:' -e '^http_proxy' \"$c\" 2>/dev/null; echo \"homepage_url:file://$start\"; "
    "echo 'toolbar_items:back/history/forward/home/url_bar/reloadstop/openmenu'; } > \"$c.new\" && mv \"$c.new\" \"$c\"\n"
    "exec netsurf-gtk3 \"$@\"")

# Ordner, Name (englisch, deutsch), Kennung, Icon aus clokeos.html (oder None), Startzeile,
# Fensterklassen, intern. Wie bei macOS heißt das Paket englisch; den deutschen Namen findet
# CloakServer in Contents/Resources/de.lproj/InfoPlist.strings.
APPS = [
    ('System/Library/CoreServices', 'Fundus', 'Fundus', 'com.clokeos.fundus', 'dateien', 'exec thunar "$@"', 'Thunar', ''),
    ('System/Applications', 'Launchpad', 'Programme', 'com.clokeos.launchpad', 'programme', None, '', 'launchpad'),
    ('System/Applications', 'Search', 'Search', 'com.clokeos.search', 'search', SEARCH_START,
     'Netsurf-gtk3 Netsurf* NetSurf*', ''),
    ('System/Applications/Utilities', 'Terminal', 'Terminal', 'com.clokeos.terminal', 'terminal',
     'exec xfce4-terminal "$@"', 'Xfce4-terminal XTerm UXTerm', ''),
    ('System/Applications/Utilities', 'Linux', 'Linux', 'com.clokeos.linux', None, term('com.clokeos.linux', 'Linux', 'Linux', '80x24', 'tux-run'), '', ''),
    ('System/Applications', 'Notes', 'Notizen', 'com.clokeos.notizen', 'notizen',
     'exec /System/Library/CoreServices/ClokeApps/clokeapps notes', '', ''),
    ('System/Applications', 'Calculator', 'Rechner', 'com.clokeos.rechner', 'rechner', 'exec /System/Library/CoreServices/ClokeApps/clokeapps calc', '', ''),
    ('System/Applications', 'Calendar', 'Kalender', 'com.clokeos.kalender', 'kalender',
     'exec /System/Library/CoreServices/ClokeApps/clokeapps calendar', '', ''),
    ('System/Applications/Utilities', 'Activity Monitor', 'Aktivitätsanzeige', 'com.clokeos.aktivitaet', 'aktivitaet',
     'exec /System/Library/CoreServices/ClokeApps/clokeapps activity', '', ''),
    ('System/Applications/Utilities', 'Disk Utility', 'Festplattendienstprogramm', 'com.clokeos.festplatte', 'hd',
     None, '', 'diskutil'),
    ('System/Applications', 'System Settings', 'Systemeinstellungen', 'com.clokeos.systemeinstellungen', 'einstellungen',
     None, '', 'settings'),
    # Windows-Programme (Wine, nur x86_64): Dateimanager von Wine mit C: = /NT; .exe öffnet Fundus direkt
    ('Applications', 'Wine', 'Wine', 'org.winehq.wine', 'wine', 'exec wine winefile "$@"', 'winefile* wine* explorer* notepad* *.exe', ''),
    # nachinstalliert wie ein Programm aus dem Netz: liegt in /Applications
    ('Applications', 'Dillo', 'Dillo', 'org.dillo.dillo', None, 'exec dillo "$@"', 'Dillo', ''),
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


def linux_icon():
    """Icon für Linux.app: Pinguin auf dunkelblauem Grund (nach der Vorlage des Nutzers nachgezeichnet)."""
    return '''<svg xmlns="http://www.w3.org/2000/svg" width="64" height="64" viewBox="0 0 64 64">
<defs>
<linearGradient id="bg" x1="0" y1="0" x2="0" y2="1"><stop offset="0" stop-color="#36496b"/><stop offset="1" stop-color="#1b2540"/></linearGradient>
<linearGradient id="body" x1="0" y1="0" x2="0" y2="1"><stop offset="0" stop-color="#2a2d3b"/><stop offset="1" stop-color="#14161f"/></linearGradient>
<linearGradient id="fl" x1="0" y1="0" x2="1" y2="0"><stop offset="0" stop-color="#4a4f62"/><stop offset=".3" stop-color="#232634"/><stop offset="1" stop-color="#0f1118"/></linearGradient>
<linearGradient id="belly" x1="0" y1="0" x2="0" y2="1"><stop offset="0" stop-color="#ffffff"/><stop offset="1" stop-color="#d8dbe6"/></linearGradient>
<linearGradient id="or" x1="0" y1="0" x2="0" y2="1"><stop offset="0" stop-color="#ffc13a"/><stop offset="1" stop-color="#f29a1a"/></linearGradient>
</defs>
<rect x="4" y="4" width="56" height="56" rx="13" fill="url(#bg)"/>
<rect x="4.4" y="4.4" width="55.2" height="55.2" rx="12.6" fill="none" stroke="#8a95ad" stroke-opacity=".55" stroke-width=".8"/>
<ellipse cx="32" cy="55" rx="16" ry="2.6" fill="#0c1020" fill-opacity=".6"/>
<path d="M32 8C25.400 8 20.800 12.500 20.600 19.500c-.100 3.300-1.800 5.800-2.600 8.800-1.100 4.400-.700 10.200 2.500 14.200 2.200 2.800 5.200 4.600 8 5.600 2.100.8 6.900.8 9 0 2.800-1 5.800-2.800 8-5.600 3.200-4 3.600-9.800 2.500-14.200-.8-3-2.500-5.500-2.600-8.800C43.200 12.500 38.600 8 32 8z" fill="url(#body)"/>
<path d="M21.600 25.200C16.200 26.600 12.600 33 10.200 43.600c-.3 1.400 1 1.700 2 .6 3.800-4.400 7.600-10.600 10.200-16.200z" fill="url(#fl)" stroke="#555b70" stroke-width=".35"/>
<path d="M42.400 25.200C47.800 26.600 51.400 33 53.800 43.600c.3 1.400-1 1.700-2 .6-3.800-4.400-7.600-10.600-10.200-16.200z" fill="url(#fl)" stroke="#555b70" stroke-width=".35"/>
<path d="M32 26.200c-5.500 0-10.900 5.200-10.900 13.500 0 6.200 3.200 10.800 10.900 10.800s10.900-4.600 10.900-10.800c0-8.300-5.400-13.500-10.900-13.500z" fill="url(#belly)"/>
<path d="M27.500 26.300c1.200 1.600 2.800 2.400 4.500 2.400s3.300-.8 4.500-2.400c-1.400-.6-2.900-.9-4.500-.9s-3.100.3-4.500.9z" fill="#c3c7d2"/>
<ellipse cx="27.300" cy="17.300" rx="3.400" ry="3.700" fill="#fff"/><ellipse cx="36.700" cy="17.300" rx="3.400" ry="3.700" fill="#fff"/>
<circle cx="28" cy="18.100" r="1.700" fill="#0d0e14"/><circle cx="36" cy="18.100" r="1.700" fill="#0d0e14"/>
<circle cx="28.500" cy="17.500" r=".55" fill="#fff"/><circle cx="36.500" cy="17.500" r=".55" fill="#fff"/>
<path d="M26.200 23.200c0-1.500 2.600-3 5.800-3s5.800 1.500 5.800 3c0 1.700-2.600 3.100-5.800 3.100s-5.800-1.400-5.800-3.100z" fill="url(#or)"/>
<path d="M26.600 23.700c1.700.9 3.400 1.300 5.400 1.300s3.700-.4 5.400-1.300" fill="none" stroke="#c9680a" stroke-width=".9" stroke-linecap="round"/>
<path d="M18.2 54.600c-.5-.9 0-1.700 1-1.900l2.600-.5c.900-.2 1.500-.9 2.300-1.300 1.400-.6 3.800-.4 5.200.4l.3 3.900c-1 .3-1.800-.1-2.500-.5-.8.700-1.700.7-2.600.2-.9.700-1.900.6-2.700 0-1 .3-2.700.3-3.600-.3z" fill="url(#or)"/>
<path d="M45.800 54.600c.5-.9 0-1.700-1-1.900l-2.600-.5c-.9-.2-1.500-.9-2.300-1.300-1.400-.6-3.800-.4-5.200.4l-.3 3.900c1 .3 1.800-.1 2.500-.5.800.7 1.700.7 2.600.2.900.7 1.900.6 2.700 0 1 .3 2.700.3 3.600-.3z" fill="url(#or)"/>
</svg>
'''


def write(path, text, mode=0o644):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, 'w', encoding='utf-8') as f:
        f.write(text)
    os.chmod(path, mode)


def ascii_name(name):
    """Ordner- und Dateinamen ohne Leerzeichen und Umlaute (Activity Monitor -> ActivityMonitor): Der
    Anzeigename steht in der Info.plist, und die debugfs-Befehle von brand.sh trennen an Leerzeichen."""
    name = name.replace(' ', '')
    assert name.isascii(), name
    return name


def main(stage, icons, version, build):
    for folder, name, name_de, ident, icon, start, classes, internal in APPS:
        exe = ascii_name(name)
        app = os.path.join(stage, folder, exe + '.app', 'Contents')
        items = [('CFBundleDevelopmentRegion', 'en'), ('CFBundleDisplayName', name), ('CFBundleExecutable', exe),
                 ('CFBundleIconFile', 'AppIcon'), ('CFBundleIdentifier', ident),
                 ('CFBundleInfoDictionaryVersion', '6.0'), ('CFBundleName', name), ('CFBundlePackageType', 'APPL'),
                 ('CFBundleShortVersionString', version), ('CFBundleVersion', build)]
        if classes:
            items.append(('CLWindowClasses', classes))
        if internal:
            items.append(('CLInternal', internal))
        write(os.path.join(app, 'Info.plist'), plist(items))
        write(os.path.join(app, 'PkgInfo'), 'APPL????')
        if name_de != name:
            write(os.path.join(app, 'Resources', 'de.lproj', 'InfoPlist.strings'),
                  f'"CFBundleDisplayName" = "{name_de}";\n"CFBundleName" = "{name_de}";\n')
        script = '#!/bin/sh\n'
        script += start + '\n' if start else f'# {name} zeichnet CloakServer selbst ({internal}).\nexit 0\n'
        write(os.path.join(app, 'MacOS', exe), script, 0o755)
        if icon:
            own = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'icons', icon + '.svg')
            with open(own if os.path.exists(own) else os.path.join(icons, icon + '.svg'), encoding='utf-8') as f:
                svg = f.read()
        elif name == 'Linux':
            svg = linux_icon()
        else:
            svg = generic_icon(name, sum(map(ord, name)) * 37 % 360)
        write(os.path.join(app, 'Resources', 'AppIcon.svg'), svg)
    write(os.path.join(stage, 'Library/Preferences/com.clokeos.dock.plist'), plist([('persistent-apps', DOCK)]))
    print('Bundles: %d Programme, Dock mit %d' % (len(APPS), len(DOCK)))


if __name__ == '__main__':
    if len(sys.argv) != 5:
        sys.exit(__doc__)
    main(*sys.argv[1:])
