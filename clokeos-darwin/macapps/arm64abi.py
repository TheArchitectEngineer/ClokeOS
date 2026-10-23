#!/usr/bin/env python3
"""Prüft cfarm64.m/cfarm64.S (arm64-Weiterleitung und NSInvocation) auf dem Mac gegen clang.

    python3 macapps/arm64abi.py [anzahl] [seed]

Der Mac ist selbst arm64, clang setzt dort Apples Aufrufkonvention um. Das Skript erzeugt zufällige
Signaturen (Ganzzahlen aller Breiten, BOOL, Zeiger, float, double, HFA aus float/double auch
verschachtelt und mit Feldern, kleine und große Strukturen, bis 14 Argumente, also auch Stack)
und übersetzt ein Testprogramm mit cfarm64.m (CLOKE_ARM64_TEST) und cfarm64.S:

  Weiterleitung  ein von clang übersetzter Aufruf geht an __CF_forwarding_prep_0; die Testfassung
                 von __CFArm64Forwarding baut daraus den Rahmen (wie für NSInvocation) und prüft
                 jedes Argument an seinem Offset; der Rückgabewert läuft den Weg zurück und wird
                 beim Aufrufer verglichen.
  Aufruf         ein Rahmen wird wie mit setArgument: gefüllt und mit cfa64_invoke (wie
                 -[NSInvocation invoke]) an eine von clang übersetzte Funktion übergeben, die
                 ihre Argumente festhält (kleine Ganzzahlen auch als int, prüft die Erweiterung
                 durch den Aufrufer) und einen Wert zurückgibt.
  Ausnahmen      eine Objective-C-Ausnahme muss durch __CFArm64Call und die Weiterleitung laufen.

Typcodes kommen von @encode, Größen von Apples NSGetSizeAndAlignment.
"""
import os, random, subprocess, sys, tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
N = int(sys.argv[1]) if len(sys.argv) > 1 else 1500
SEED = int(sys.argv[2]) if len(sys.argv) > 2 else 1
rnd = random.Random(SEED)

SCALARS = [  # (C-Typ, Größe, Art)
    ('char', 1, 'i'), ('unsigned char', 1, 'u'), ('short', 2, 'i'), ('unsigned short', 2, 'u'),
    ('int', 4, 'i'), ('unsigned int', 4, 'u'), ('long', 8, 'i'), ('unsigned long long', 8, 'u'),
    ('bool', 1, 'b'), ('void *', 8, 'p'), ('float', 4, 'f'), ('double', 8, 'd'),
]
structs = []   # (Name, Definition, Größe)


def scalar_value(kind, size):
    if kind == 'f':
        return repr(rnd.choice([-1.5, 3.25, 1e10, -7.0, 0.125]) + rnd.randint(-999, 999)) + 'f'
    if kind == 'd':
        return repr(rnd.choice([-2.5, 6.75, 1e100, -1e-3]) + rnd.randint(-99999, 99999))
    if kind == 'b':
        return rnd.choice(['true', 'false'])
    if kind == 'p':
        return '(void *)0x%xULL' % rnd.getrandbits(47)
    bits = size * 8
    if kind == 'u':
        return '0x%xU' % rnd.getrandbits(bits) + ('LL' if size == 8 else '')
    v = rnd.getrandbits(bits) - (1 << (bits - 1))   # auch negativ, prüft die Vorzeichen-Erweiterung
    return '(%s)%dLL' % ({1: 'char', 2: 'short', 4: 'int', 8: 'long'}[size], v)


def new_struct():
    """Struktur ohne Füllbytes (Felder nach Ausrichtung absteigend, am Ende mit char aufgefüllt)."""
    name = 'S%d' % len(structs)
    shape = rnd.random()
    fields = []
    if shape < 0.35:                      # HFA, auch verschachtelt oder als Feld
        base = rnd.choice(['float', 'double'])
        n = rnd.randint(1, 4)
        if n >= 2 and rnd.random() < 0.3:
            inner = 'H%d' % len(structs)
            k = rnd.randint(1, n - 1)
            structs.append((inner, 'struct %s { %s }' % (inner, ' '.join('%s m%d;' % (base, i) for i in range(k))), None))
            fields = [('struct ' + inner, None)] + [(base, None)] * (n - k)
            sizes = None
        elif n >= 2 and rnd.random() < 0.3:
            fields = [('%s[%d]' % (base, n), None)]
        else:
            fields = [(base, None)] * n
        body = []
        for i, (t, _) in enumerate(fields):
            if t.endswith(']'):
                b, cnt = t[:-1].split('[')
                body.append('%s m%d[%s];' % (b, i, cnt))
            else:
                body.append('%s m%d;' % (t, i))
    elif shape < 0.5:                     # 5-8 double: keine HFA, größer als 16 -> per Verweis
        n = rnd.randint(5, 8)
        body = ['double m%d;' % i for i in range(n)]
    else:                                 # gemischt
        n = rnd.randint(1, 6)
        pool = [s for s in SCALARS if s[2] != 'p']
        picks = sorted((rnd.choice(pool) for _ in range(n)), key=lambda s: -s[1])
        body = ['%s m%d;' % (t, i) for i, (t, _, _) in enumerate(picks)]
        size = sum(s[1] for s in picks)
        align = max(s[1] for s in picks)
        i = len(picks)
        while size % align:
            body.append('char m%d;' % i)
            i += 1
            size += 1
    structs.append((name, 'struct %s { %s }' % (name, ' '.join(body)), None))
    return 'struct ' + name


def rand_type(allow_void=False):
    r = rnd.random()
    if allow_void and r < 0.1:
        return 'void'
    if r < 0.45 or (r < 0.6 and structs):
        if r >= 0.45:
            return 'struct ' + rnd.choice(structs)[0]
        return new_struct()
    return rnd.choice(SCALARS)[0]


def struct_value(name):
    """Initialisierer für eine Struktur aus den gespeicherten Definitionen"""
    defn = dict((n, d) for n, d, _ in structs)[name]
    body = defn[defn.index('{') + 1:defn.rindex('}')].strip().rstrip(';').split(';')
    vals = []
    for f in body:
        f = f.strip()
        t, fname = f.rsplit(' ', 1)
        if fname.endswith(']'):
            fname, cnt = fname[:-1].split('[')
            vals.append('{' + ', '.join(value_of(t) for _ in range(int(cnt))) + '}')
        else:
            vals.append(value_of(t))
    return '{' + ', '.join(vals) + '}'


def value_of(t):
    if t.startswith('struct '):
        return struct_value(t[7:])
    for name, size, kind in SCALARS:
        if name == t:
            return scalar_value(kind, size)
    raise ValueError(t)


SMALL_INT = {'char', 'unsigned char', 'short', 'unsigned short', 'bool'}

cases = []
for k in range(N):
    nargs = rnd.randint(1, 14)
    args = [rand_type() for _ in range(nargs)]
    ret = rand_type(allow_void=True)
    cases.append((ret, args))

out = []
w = out.append
w('#define CLOKE_ARM64_TEST 1')
w('#include "%s/cfarm64.m"' % HERE)
w('#include <stdbool.h>')
for name, defn, _ in structs:
    w(defn + ';')
w('''
static NSMethodType *cur_types; static NSUInteger cur_count; static const void *cur_ret; static int cur_throw;
static unsigned char cur_frame[4096]; static int failures;
static void mktypes(const char **enc, NSUInteger n, NSMethodType *t) {
    for (NSUInteger i = 0; i < n; i++) {
        NSUInteger s = 0, a = 0;
        NSGetSizeAndAlignment(enc[i], &s, &a);
        t[i].size = s; t[i].alignment = a; t[i].type = (char *)enc[i]; t[i].offset = 0;
    }
}
id __CFArm64Forwarding(cfa64_saved *s, cfa64_ret *rr) {
    cfa64_arg a[32]; NSUInteger stacklen; cfa64_retinfo r;
    NSUInteger flen = cfa64_layout(cur_types, cur_count, a, 1, &stacklen);
    if (flen > sizeof cur_frame) abort();
    memset(cur_frame, 0, sizeof cur_frame);
    cfa64_gather(a, cur_count, stacklen, s, cur_frame);
    if (cur_throw) @throw [NSException exceptionWithName:@"Test" reason:@"durch die Weiterleitung" userInfo:nil];
    cfa64_retclass(&cur_types[0], &r);
    memset(rr, 0, sizeof *rr);
    if (r.kind == R_IND) memcpy((void *)s->x8, cur_ret, r.size);
    else if (r.kind != R_VOID) cfa64_mem_to_ret(&r, cur_ret, rr);
    return nil;
}
void __CFArm64BlockForwarding(cfa64_saved *s, cfa64_ret *rr) { abort(); }
extern void _CF_forwarding_prep_0(void);
static unsigned char rec[2048]; static int recx[32];
static void check(int ok, int k, const char *what, int i) {
    if (!ok) { failures++; if (failures < 30) printf("FEHLER Fall %d: %s %d\\n", k, what, i); }
}
''')

for k, (ret, args) in enumerate(cases):
    params = ', '.join('%s a%d' % (t, i) for i, t in enumerate(args))
    w('/* Fall %d */' % k)
    for i, t in enumerate(args):
        w('static const %s v%d_%d = %s;' % (t, k, i, value_of(t) if t.startswith('struct') else value_of(t)))
    if ret != 'void':
        w('static const %s r%d = %s;' % (ret, k, value_of(ret)))
    # Aufgerufene Funktion für den Aufruf-Test
    w('static %s callee%d(%s) {' % (ret, k, params))
    pos = 0
    for i, t in enumerate(args):
        w('    memcpy(rec + %d, &a%d, sizeof a%d);' % (pos, i, i))
        pos += 64
        if t in SMALL_INT:
            w('    recx[%d] = (int)a%d;' % (i, i))
    if ret != 'void':
        w('    return r%d;' % k)
    w('}')
    w('static void test%d(void) {' % k)
    encs = [ret] + args
    w('    static const char *enc[] = { %s };' % ', '.join('@encode(%s)' % t for t in encs))
    w('    NSMethodType t[%d]; mktypes(enc, %d, t);' % (len(encs), len(encs)))
    # Weiterleitung
    w('    cur_types = t; cur_count = %d; cur_throw = 0;' % len(encs))
    w('    typedef %s (*F)(%s);' % (ret, ', '.join(args)))
    call = '((F)_CF_forwarding_prep_0)(%s)' % ', '.join('v%d_%d' % (k, i) for i in range(len(args)))
    if ret != 'void':
        w('    cur_ret = &r%d;' % k)
        w('    %s got; memset(&got, 0x5a, sizeof got); got = %s;' % (ret, call))
        w('    check(memcmp(&got, &r%d, sizeof got) == 0, %d, "Weiterleitung Rückgabe", 0);' % (k, k))
    else:
        w('    %s;' % call)
    w('    cfa64_arg a[32]; NSUInteger flen = cfa64_layout(t, %d, a, 1, NULL);' % len(encs))
    for i, t in enumerate(args):
        w('    check(memcmp(cur_frame + t[%d].offset, &v%d_%d, sizeof v%d_%d) == 0, %d, "Weiterleitung Argument", %d);'
          % (i + 1, k, i, k, i, k, i))
    # Aufruf
    w('    unsigned char *frame = calloc(1, flen); unsigned char retbuf[256]; memset(retbuf, 0x5a, sizeof retbuf);')
    for i, t in enumerate(args):
        w('    memcpy(frame + t[%d].offset, &v%d_%d, sizeof v%d_%d);' % (i + 1, k, i, k, i))
        if t in SMALL_INT:   # Reste eines früheren Werts im Registerplatz, wie nach setArgument:
            w('    if (a[%d].kind == K_GPR) memset(frame + t[%d].offset + sizeof v%d_%d, 0xa5, 8 - sizeof v%d_%d);'
              % (i + 1, i + 1, k, i, k, i))
    w('    memset(rec, 0, sizeof rec);')
    w('    cfa64_invoke(t, %d, (void *)callee%d, retbuf, frame);' % (len(encs), k))
    pos = 0
    for i, t in enumerate(args):
        w('    check(memcmp(rec + %d, &v%d_%d, sizeof v%d_%d) == 0, %d, "Aufruf Argument", %d);' % (pos, k, i, k, i, k, i))
        pos += 64
        if t in SMALL_INT:
            w('    check(recx[%d] == (int)v%d_%d, %d, "Aufruf Erweiterung", %d);' % (i, k, i, k, i))
    if ret != 'void':
        w('    check(memcmp(retbuf, &r%d, sizeof r%d) == 0, %d, "Aufruf Rückgabe", 0);' % (k, k, k))
    w('    free(frame);')
    w('}')

w('static void thrower(int a, double b) { @throw [NSException exceptionWithName:@"Test" reason:@"durch den Aufruf" userInfo:nil]; }')
w('int main(void) { @autoreleasepool {')
for k in range(N):
    w('    test%d();' % k)
w('''    int caught = 0;
    static const char *enc[] = { @encode(void), @encode(int), @encode(double) };
    NSMethodType t[3]; mktypes(enc, 3, t);
    cfa64_arg a[3]; NSUInteger flen = cfa64_layout(t, 3, a, 1, NULL);
    unsigned char *frame = calloc(1, flen), retbuf[64];
    @try { cfa64_invoke(t, 3, (void *)thrower, retbuf, frame); } @catch (NSException *e) { caught++; }
    cur_types = t; cur_count = 3; cur_throw = 1;
    @try { ((void (*)(int, double))_CF_forwarding_prep_0)(1, 2.0); } @catch (NSException *e) { caught++; }
    check(caught == 2, -1, "Ausnahmen", caught);
    printf("%d Fälle, %d Fehler\\n", ''' + str(N) + ''', failures);
    return failures != 0;
} }''')

tmp = tempfile.mkdtemp(prefix='arm64abi-')
src = os.path.join(tmp, 'abitest.m')
with open(src, 'w') as f:
    f.write('\n'.join(out) + '\n')
exe = os.path.join(tmp, 'abitest')
cc = ['clang', '-arch', 'arm64', '-O1', '-w', '-fobjc-exceptions', '-framework', 'Foundation',
      '-o', exe, src, os.path.join(HERE, 'cfarm64.S')]
r = subprocess.run(cc, capture_output=True, text=True)
if r.returncode:
    print(r.stderr[:4000])
    print('Quelltext:', src)
    sys.exit(2)
r = subprocess.run([exe])
print('Testprogramm:', src)
sys.exit(r.returncode)
