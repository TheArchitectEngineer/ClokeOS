#!/usr/bin/env python3
"""Erzeugt die Startseiten von Search (NetSurf kann kein Flexbox und keine CSS-Variablen, deshalb Tabellen):
overlay-desktop/share/clokeos/search-<sprache>-<hell|dunkel>.html, nach dem Prototyp (clokeos.html, mountSearch)."""
import os

OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'overlay-desktop/share/clokeos')
TXT = {
    'en': ('Search the web or enter an address', 'FAVORITES', 'new tab'),
    'de': ('Im Web suchen oder Adresse eingeben', 'FAVORITEN', 'neuer Tab'),
    'fr': ('Rechercher sur le web ou saisir une adresse', 'FAVORIS', 'nouvel onglet'),
    'es': ('Buscar en la web o escribir una dirección', 'FAVORITOS', 'pestaña nueva'),
    'it': ('Cerca sul web o inserisci un indirizzo', 'PREFERITI', 'nuova scheda'),
}
FAV = [('ClokeOS', 'https://github.com/cloke-tech/ClokeOS', '#8a63f2', 'C'),
       ('Releases', 'https://github.com/cloke-tech/ClokeOS/releases', '#3fa874', 'R'),
       ('PureDarwin', 'https://wiki.puredarwin.org/', '#3b82f6', 'P'),
       ('Darwin', 'https://opensource.apple.com/', '#e0623b', 'D'),
       ('Wikipedia', 'https://en.wikipedia.org/', '#6b6a73', 'W'),
       ('DuckDuckGo', 'https://duckduckgo.com/', '#d4793b', 'D')]
THEME = {
    'hell': dict(bg='#f6f4fb', fg='#1e1a27', sub='#8b8697', field='#ffffff', line='#ddd8ea', tile='#4a4558', title='#1e1a27'),
    'dunkel': dict(bg='#1c1b22', fg='#f4f2f8', sub='#9a96a6', field='#2c2b33', line='#3b3a45', tile='#d9d6e2', title='#f4f2f8'),
}
for lang, (ph, fav, ext) in TXT.items():
    for th, c in THEME.items():
        rows = ''
        for i in range(0, len(FAV), 3):
            rows += '<tr>' + ''.join(
                '<td><a href="%s"><b style="background:%s">%s</b>%s</a></td>' % (u, col, ch, n) for n, u, col, ch in FAV[i:i + 3]) + '</tr>\n'
        html = f'''<!DOCTYPE html>
<html><head><meta charset="utf-8"><title>Search</title>
<style>
body {{ margin: 0; background: {c['bg']}; font-family: Inter, sans-serif; color: {c['fg']}; text-align: center; }}
h1 {{ margin: 96px 0 26px; font-size: 44px; font-weight: 700; color: {c['title']}; }}
form {{ margin: 0 auto 52px; width: 560px; }}
input {{ width: 100%; box-sizing: border-box; padding: 14px 24px; font-size: 16px; border: 1px solid {c['line']}; border-radius: 28px; background: {c['field']}; color: {c['fg']}; }}
h2 {{ margin: 0 0 14px; text-align: left; width: 420px; margin-left: auto; margin-right: auto; font-size: 11px; letter-spacing: 1px; color: {c['sub']}; font-weight: 700; }}
table {{ margin: 0 auto; border-spacing: 26px 12px; }}
td {{ width: 90px; font-size: 12px; color: {c['tile']}; }}
td a {{ color: {c['tile']}; text-decoration: none; display: block; }}
td a b {{ display: block; width: 54px; height: 54px; line-height: 54px; margin: 0 auto 7px; border-radius: 14px; color: #fff; font-size: 22px; }}
</style></head><body>
<h1>Search</h1>
<form action="https://duckduckgo.com/" method="get"><input type="text" name="q" placeholder="{ph}"></form>
<h2>{fav}</h2>
<table>
{rows}</table>
</body></html>
'''
        open(os.path.join(OUT, f'search-{lang}-{th}.html'), 'w', encoding='utf-8').write(html)
print('ok')
