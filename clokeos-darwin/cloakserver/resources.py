#!/usr/bin/env python3
"""Holt Logo und Icons für CloakServer aus clokeos.html, damit Image und Oberflächen-Prototyp
dieselben Bilder haben.

  resources.py <clokeos.html> <zielordner>

Die Dock-Icons entstehen in clokeos.html aus JavaScript (ICON-Objekt). Das Skript lässt genau
diesen Teil von macOS' eigener JavaScript-Engine ausführen (osascript -l JavaScript) und schreibt
jedes Icon als eigenständige SVG-Datei mit den Verläufen aus dem <defs>-Block. Platzhalter, die
CloakServer zur Laufzeit ersetzt: @MON@ und @DAY@ (Kalender), @FILL@ (Farbe von Logo und
Menüleistensymbolen).
"""
import json
import os
import re
import subprocess
import sys
import tempfile

DOCK_ICONS = ['dateien', 'programme', 'search', 'terminal', 'notizen', 'rechner', 'kalender',
              'aktivitaet', 'einstellungen', 'info', 'folder']
MENUBAR_ICONS = {'mb-wifi': 'wifi', 'mb-spot': 'spot', 'mb-cc': 'cc'}


def between(text, start, end_line_prefix):
    """Zeilen ab der Zeile mit start bis einschließlich der ersten Zeile, die mit
    end_line_prefix beginnt."""
    lines = text.split('\n')
    i = next(n for n, l in enumerate(lines) if l.startswith(start))
    j = next(n for n in range(i, len(lines)) if lines[n].startswith(end_line_prefix))
    return lines[i:j + 1]


def run_icons(html):
    code = between(html, 'const LOGO_D =', 'const icon = ')
    code = [l for l in code if not l.startswith('$$(')]  # DOM-Zugriffe gibt es hier nicht
    js = '''
function MockDate() { return { toLocaleDateString: () => '@MON@', getDate: () => '@DAY@' }; }
(function (Date) {
%s
  const out = { LOGO_D };
  for (const id of %s) out[id] = icon(id);
  out.papierkorb = icon('papierkorb', false);
  out['papierkorb-voll'] = icon('papierkorb', true);
  return JSON.stringify(out);
})(MockDate);
''' % ('\n'.join(code), json.dumps(DOCK_ICONS))
    with tempfile.NamedTemporaryFile('w', suffix='.js', delete=False) as f:
        f.write(js)
        path = f.name
    try:
        res = subprocess.run(['osascript', '-l', 'JavaScript', path], capture_output=True, text=True)
    finally:
        os.unlink(path)
    if res.returncode != 0:
        sys.exit('osascript: ' + res.stderr.strip())
    return json.loads(res.stdout)


def standalone(svg, defs, size):
    """<svg viewBox=...> aus der Seite in eine eigenständige Datei mit Verläufen verwandeln."""
    m = re.match(r'<svg([^>]*)>(.*)</svg>$', svg, re.S)
    attrs, body = m.group(1), m.group(2)
    attrs = re.sub(r'\s(width|height)="[^"]*"', '', attrs)
    return ('<svg xmlns="http://www.w3.org/2000/svg" width="%d" height="%d"%s>%s%s</svg>\n'
            % (size[0], size[1], attrs, defs, body))


def main(html_path, out):
    html = open(html_path, encoding='utf-8').read()
    defs = re.search(r'<defs>.*?</defs>', html, re.S).group(0)
    # Der Filter für die Lichtbrechung im Browser braucht CloakServer nicht.
    defs = re.sub(r'\s*<filter.*?</filter>', '', defs, flags=re.S)
    os.makedirs(os.path.join(out, 'icons'), exist_ok=True)

    icons = run_icons(html)
    logo_d = icons.pop('LOGO_D')
    for name, svg in icons.items():
        with open(os.path.join(out, 'icons', name + '.svg'), 'w', encoding='utf-8') as f:
            f.write(standalone(svg, defs, (64, 64)))

    with open(os.path.join(out, 'logo.svg'), 'w', encoding='utf-8') as f:
        f.write('<svg xmlns="http://www.w3.org/2000/svg" width="64" height="64" viewBox="0 0 64 64">'
                '<path fill="@FILL@" fill-rule="evenodd" d="%s"/></svg>\n' % logo_d)
    # wie logoBig() im Prototyp: mit dem violetten Verlauf g-logo
    with open(os.path.join(out, 'logo-gradient.svg'), 'w', encoding='utf-8') as f:
        f.write(standalone('<svg viewBox="0 0 64 64"><path fill="url(#g-logo)" fill-rule="evenodd" d="%s"/></svg>'
                           % logo_d, defs, (64, 64)))

    for button, name in MENUBAR_ICONS.items():
        m = re.search(r'id="%s"[^>]*>(?:<span[^>]*>[^<]*</span>)?(<svg.*?</svg>)' % button, html, re.S)
        svg = m.group(1).replace('currentColor', '@FILL@')
        vb = [float(v) for v in re.search(r'viewBox="([^"]+)"', svg).group(1).split()]
        with open(os.path.join(out, name + '.svg'), 'w', encoding='utf-8') as f:
            f.write(standalone(svg, '', (int(vb[2]), int(vb[3]))))
    print('Ressourcen: %d Icons, Logo, %d Menüleistensymbole' % (len(icons), len(MENUBAR_ICONS)))


if __name__ == '__main__':
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    main(sys.argv[1], sys.argv[2])
