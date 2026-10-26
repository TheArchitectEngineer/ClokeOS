#!/usr/bin/env python3
"""Prüft die Übersetzungen: Welche Texte in L("…") (und in den Feldern, die erst bei der Ausgabe
durch L() gehen) fehlen in strings-de.h, und welche Einträge dort braucht kein Code mehr.

  cloakserver/l10n-check.py        Ausgabe der fehlenden und überzähligen Texte, Exit 1 bei fehlenden
"""
import os
import re
import sys

here = os.path.dirname(os.path.abspath(__file__))
srcs = [os.path.join(here, f) for f in sorted(os.listdir(here)) if f.endswith('.c')]
srcs.append(os.path.join(here, '..', 'recovery', 'installer.c'))


def c_strings(expr):
    """Verkettete C-Literale "a" "b" zu einem Text"""
    parts = re.findall(r'"((?:[^"\\]|\\.)*)"', expr)
    s = ''.join(parts)
    return bytes(s, 'utf-8').decode('unicode_escape').encode('latin-1').decode('utf-8')


used = set()
for p in srcs:
    code = open(p, encoding='utf-8').read()
    code = re.sub(r'/\*.*?\*/', '', code, flags=re.S)
    for m in re.finditer(r'\bL\(\s*((?:"(?:[^"\\]|\\.)*"\s*)+)\)', code):
        used.add(c_strings(m.group(1)))
    # L(a ? "x" : "y") und fail("…", …) im Installer, der das Format selbst übersetzt: Argumente
    # bis zur passenden Klammer, Klammern in Strings zählen nicht
    for m in re.finditer(r'\b(?:L|fail|own_window)\(', code):
        i, depth, args = m.end(), 1, ''
        while i < len(code) and depth:
            ch = code[i]
            if ch == '"':
                j = i + 1
                while j < len(code) and code[j] != '"':
                    j += 2 if code[j] == '\\' else 1
                args += code[i:j + 1]
                i = j + 1
                continue
            depth += ch == '('
            depth -= ch == ')'
            args += ch
            i += 1
        for lit in re.findall(r'((?:"(?:[^"\\]|\\.)*"\s*)+)', args):
            t = c_strings(lit)
            if re.search(r'[A-Za-z]{2}', t) and '%s/' not in t:
                used.add(t)
    # Felder, deren Einträge erst bei der Ausgabe L() durchlaufen (siehe Kommentare im Code)
    for name in ('head', 'sub', 'POWER_LABEL', 'ST_NAMES', 'names', 'sizes', 'labels', 'ROPT', 'REGIONS', 'ART_TITLES', 'PROC_TITLES'):
        for m in re.finditer(r'\b' + name + r'\b[^=;{]*=\s*\{(.*?)\};', code, flags=re.S):
            for lit in re.findall(r'"((?:[^"\\]|\\.)*)"', m.group(1)):
                s = c_strings('"' + lit + '"')
                if re.search(r'[A-Za-z]{2}', s) and not s.endswith('.svg') and not re.match(r'^[A-Z]{3,4}-?\d', s):
                    used.add(s)
    for m in re.finditer(r'const char \*sub = (.*?);', code, flags=re.S):
        for lit in re.findall(r'"((?:[^"\\]|\\.)*)"', m.group(1)):
            used.add(c_strings('"' + lit + '"'))

table = {}
for line in open(os.path.join(here, 'strings-de.h'), encoding='utf-8'):
    m = re.match(r'\s*\{\s*("(?:[^"\\]|\\.)*")\s*,\s*("(?:[^"\\]|\\.)*")\s*\},', line)
    if m:
        table[c_strings(m.group(1))] = c_strings(m.group(2))

# Texte, die zur Laufzeit durch L() gehen (Partitions- und Kontonamen)
used |= {'EFI System Partition', 'Guest'}
same = {'hello', 'hallo', 'bonjour', 'hola', 'ciao', 'olá', 'hej', '@FILL@', 'Terminal', 'OK', 'Kernel', 'Build', 'Search', 'Fundus', 'ClokeOS', 'Dillo', 'English', 'Deutsch', 'Partition',
        'Launchpad', 'System Administrator', 'Velvet', 'Bokeh'}
missing = sorted(s for s in used if s not in table and s not in same and not re.fullmatch(r'[\W\d_]*', s))
unused = sorted(s for s in table if s not in used)
for s in missing:
    print('fehlt:', repr(s))
for s in unused:
    print('ungenutzt:', repr(s))
print(f'{len(used)} Texte im Code, {len(table)} Übersetzungen, {len(missing)} fehlen, {len(unused)} ungenutzt')
sys.exit(1 if missing else 0)
