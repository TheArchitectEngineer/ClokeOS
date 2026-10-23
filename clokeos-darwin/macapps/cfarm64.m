/* arm64-Teil von Darlings NSMethodSignature, NSInvocation und Weiterleitung für ClokeOS.
 *
 * Darling rechnet die Lage der Argumente nur nach der x86_64-Konvention. Hier steht Apples
 * arm64-Konvention (DarwinPCS, eine Abwandlung von AAPCS64), wie clang sie umsetzt:
 *
 *   - Ganzzahlen, Zeiger und Strukturen bis 16 Byte (ohne HFA) in x0-x7, eine Struktur nur ganz
 *     oder gar nicht (passt sie nicht mehr, sind alle x-Register verbraucht); 16-Byte-Ganzzahlen
 *     ('t'/'T') beginnen bei einem geraden Register. Bis 32 Bit erweitert der Aufrufer (char,
 *     short, BOOL).
 *   - float und double in v0-v7 (d0-d7).
 *   - HFA (Struktur aus 1-4 gleichen float oder double, z. B. CGPoint, CGSize, CGRect,
 *     NSEdgeInsets): jedes Glied in einem eigenen v-Register; passen nicht alle, kommt die ganze
 *     Struktur auf den Stack und alle v-Register gelten als verbraucht.
 *   - größere Strukturen (CGAffineTransform …) per Verweis: der Aufrufer legt eine Kopie an und
 *     übergibt deren Adresse.
 *   - der Stack ist dicht gepackt (Apples Abweichung von AAPCS64): char 1 Byte, short 2, float 4,
 *     HFA mit der Ausrichtung ihres Glieds; Strukturen bis 16 Byte in 8-Byte-Schritten.
 *   - Rückgabe in x0/x1, d0-d3 (HFA) oder über x8 (Strukturen über 16 Byte, keine HFA).
 *     objc_msgSend gibt es nur in einer Fassung (kein _stret).
 *
 * Rahmen (frame) einer NSInvocation, auf den sich die Offsets der NSMethodSignature beziehen:
 *
 *   0x00 x0-x7   0x40 x8   0x50 d0-d7   0x90 Stack-Argumente   danach: Schattenbereich
 *
 * Jedes Argument liegt im Rahmen zusammenhängend an seinem Offset, so wie getArgument:/
 * setArgument: und Foundations Port-Coder (_idxToArg:) es erwarten. HFA aus double liegen in
 * aufeinanderfolgenden d-Plätzen ohnehin richtig; float-HFA in Registern und Strukturen per
 * Verweis liegen im Schattenbereich und werden beim Aufruf (__CFArm64Invoke) in die Register
 * verteilt bzw. als Adresse übergeben, bei der Weiterleitung (__CFArm64Forwarding)
 * eingesammelt. Der Rahmenkopf ist zugleich der Registerblock für __CFArm64Call (cfarm64.S).
 *
 * Geprüft auf dem Mac (selbst arm64) gegen clang: macapps/arm64abi.py erzeugt zufällige
 * Signaturen und vergleicht Weiterleitung und Aufruf mit von clang übersetzten Aufrufen. */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <alloca.h>
#include <stdio.h>

#if defined(__arm64__)

#ifdef CLOKE_ARM64_TEST
#import <Foundation/Foundation.h>
typedef struct {
    NSUInteger size;
    NSUInteger alignment;
    size_t offset;
    char *type;
} NSMethodType;
#define CFA64_HIDDEN static
#else
#import <Foundation/NSInvocation.h>
#import <Foundation/NSMethodSignature.h>
#import <CoreFoundation/CFLogUtilities.h>
#import <objc/runtime.h>
#import <objc/message.h>
#import <Block_private.h>
#import "NSInvocationInternal.h"
#import "NSBlockInvocationInternal.h"
#import "NSZombie.h"
#define CFA64_HIDDEN __attribute__((visibility("hidden")))
#endif

enum {
    CFA64_X = 0x00, CFA64_X8 = 0x40, CFA64_D = 0x50, CFA64_STACK = 0x90,
    CFA64_NOREG = 0xff,
};

/* von den Weiterleitungs-Einsprüngen in cfarm64.S gesichert */
typedef struct {
    uint64_t x[8];
    uint64_t x8;
    uint64_t stack;         /* Adresse der Stack-Argumente des Aufrufers */
    uint64_t q[8][2];       /* q0-q7, untere Hälfte = d */
} cfa64_saved;

typedef struct {
    uint64_t x[2];
    uint64_t d[4];
} cfa64_ret;

enum { K_GPR = 1, K_FPR, K_HFA, K_STACK, K_REF };
enum { R_VOID = 0, R_GPR, R_FPR, R_HFAF, R_IND };

typedef struct {
    uint8_t kind;
    uint8_t reg;            /* erstes x- bzw. d-Register; K_REF: CFA64_NOREG = Adresse auf dem Stack */
    uint8_t n;              /* Register (K_GPR) bzw. Glieder (K_HFA) */
    uint8_t msize;          /* K_HFA: 4 (float) oder 8 (double) */
    char ext;               /* K_GPR, kleine Ganzzahl: Typzeichen für die Erweiterung auf 32 Bit */
    uint32_t size;
    uint32_t stack;         /* Offset im Stack-Bereich (K_STACK, K_REF ohne Register) */
    uint32_t offset;        /* Offset im Rahmen (= NSMethodType.offset) */
} cfa64_arg;

typedef struct {
    int kind;
    uint32_t size;
    uint8_t n;
    char ext;               /* char, short, BOOL: der Aufgerufene erweitert auf 32 Bit */
} cfa64_retinfo;

void __CFArm64Call(void *fn, const void *regs, const void *stack, size_t stacklen, void *ret);

/* --- Typen ---------------------------------------------------------------------------------- */
static const char *cfa64_quals(const char *t) {
    while (*t && strchr("rnNoORVA", *t)) t++;
    return t;
}

/* Geht einen Typ durch und zählt die Glieder einer möglichen HFA. *ok wird 0, sobald etwas
 * anderes als float/double vorkommt (auch Unions und Bitfelder: clang behandelt Unions zwar
 * eigens, in Methodensignaturen kommen sie aber praktisch nicht vor). */
static const char *cfa64_walk(const char *t, char *base, unsigned *count, int *ok) {
    t = cfa64_quals(t);
    switch (*t) {
    case 'f': case 'd': case 'D': {
        char b = *t == 'f' ? 'f' : 'd';
        if (*base && *base != b) *ok = 0;
        *base = b;
        (*count)++;
        return t + 1;
    }
    case '{': case '(': {
        char close = *t == '{' ? '}' : ')';
        if (*t == '(') *ok = 0;
        const char *q = t + 1;
        while (*q && *q != '=' && *q != close) q++;
        if (*q != '=') { *ok = 0; return *q ? q + 1 : q; }
        t = q + 1;
        while (*t && *t != close) {
            if (*t == '"') {
                const char *e = strchr(t + 1, '"');
                if (!e) return t + strlen(t);
                t = e + 1;
                continue;
            }
            const char *nt = cfa64_walk(t, base, count, ok);
            if (nt == t) { *ok = 0; return t + strlen(t); }
            t = nt;
        }
        return *t ? t + 1 : t;
    }
    case '[': {
        char *e;
        unsigned long n = strtoul(t + 1, &e, 10);
        unsigned before = *count;
        t = cfa64_walk(e, base, count, ok);
        *count = before + (unsigned)((*count - before) * n);
        while (*t && *t != ']') t++;
        return *t ? t + 1 : t;
    }
    case '^': {
        char b = 0; unsigned c = 0; int o = 1;
        *ok = 0;
        if (t[1] == '?') return t + 2;
        return cfa64_walk(t + 1, &b, &c, &o);
    }
    case '@':
        *ok = 0;
        if (t[1] == '?') return t + 2;
        if (t[1] == '"') { const char *e = strchr(t + 2, '"'); return e ? e + 1 : t + strlen(t); }
        return t + 1;
    case 'b':
        *ok = 0;
        t++;
        while (*t >= '0' && *t <= '9') t++;
        return t;
    case 0:
        return t;
    default:
        *ok = 0;
        return t + 1;
    }
}

/* HFA? Dann Gliedgröße (4/8) und Anzahl, sonst 0. */
static int cfa64_hfa(const char *type, NSUInteger size, unsigned *members) {
    char base = 0; unsigned count = 0; int ok = 1;
    cfa64_walk(type, &base, &count, &ok);
    if (!ok || count < 1 || count > 4) return 0;
    int msize = base == 'f' ? 4 : 8;
    if ((NSUInteger)msize * count != size) return 0;
    *members = count;
    return msize;
}

static NSUInteger cfa64_align(NSUInteger v, NSUInteger a) { return a > 1 ? (v + a - 1) / a * a : v; }

static void cfa64_retclass(const NSMethodType *t, cfa64_retinfo *r) {
    const char *c = cfa64_quals(t->type);
    unsigned members;
    int msize;
    r->size = (uint32_t)t->size;
    r->n = 0;
    r->ext = strchr("csCSB", *c) && *c && t->size < 4 ? *c : 0;
    switch (*c) {
    case 'v':
        r->kind = R_VOID;
        return;
    case 'f': case 'd': case 'D':
        r->kind = R_FPR;
        return;
    case '{': case '(': case '[':
        if ((msize = cfa64_hfa(c, t->size, &members))) {
            r->kind = msize == 4 ? R_HFAF : R_FPR;
            r->n = members;
        } else {
            r->kind = t->size > 16 ? R_IND : R_GPR;
        }
        return;
    default:
        r->kind = R_GPR;
        return;
    }
}

/* Lage aller Argumente (types[1…count-1]; types[0] ist der Rückgabewert) nach out[1…count-1].
 * Mit set_offsets werden die Offsets auch in types eingetragen. Gibt die Rahmenlänge zurück, in
 * *stacklen die Größe des Stack-Bereichs. */
static NSUInteger cfa64_layout(NSMethodType *types, NSUInteger count, cfa64_arg *out, int set_offsets,
                               NSUInteger *stacklen) {
    unsigned ngrn = 0, nsrn = 0;
    NSUInteger nsaa = 0;

    for (NSUInteger i = 1; i < count; i++) {
        cfa64_arg *a = &out[i];
        const char *c = cfa64_quals(types[i].type);
        NSUInteger size = types[i].size, align = types[i].alignment ? types[i].alignment : 1;
        unsigned members = 0;
        int msize = 0;
        memset(a, 0, sizeof *a);
        a->size = (uint32_t)size;

        switch (*c) {
        case 'f': case 'd': case 'D':
            if (nsrn < 8) {
                a->kind = K_FPR; a->reg = (uint8_t)nsrn++; a->n = 1;
                a->offset = CFA64_D + 8 * a->reg;
            } else {
                nsaa = cfa64_align(nsaa, size);
                a->kind = K_STACK; a->stack = (uint32_t)nsaa; nsaa += size;
            }
            break;

        case '{': case '(': case '[':
            if ((msize = cfa64_hfa(c, size, &members))) {
                if (nsrn + members <= 8) {
                    a->kind = K_HFA; a->reg = (uint8_t)nsrn; a->n = (uint8_t)members; a->msize = (uint8_t)msize;
                    nsrn += members;
                    if (msize == 8) a->offset = CFA64_D + 8 * a->reg;   /* float: Schatten, unten */
                } else {
                    nsrn = 8;
                    nsaa = cfa64_align(nsaa, msize);
                    a->kind = K_STACK; a->stack = (uint32_t)nsaa; nsaa += size;
                }
            } else if (size > 16) {
                a->kind = K_REF;
                if (ngrn < 8) {
                    a->reg = (uint8_t)ngrn++;
                } else {
                    a->reg = CFA64_NOREG;
                    nsaa = cfa64_align(nsaa, 8);
                    a->stack = (uint32_t)nsaa; nsaa += 8;
                }
            } else {
                unsigned regs = (unsigned)((size + 7) / 8);
                if (align >= 16) ngrn = (ngrn + 1) & ~1u;
                if (ngrn + regs <= 8) {
                    a->kind = K_GPR; a->reg = (uint8_t)ngrn; a->n = (uint8_t)regs;
                    a->offset = CFA64_X + 8 * a->reg;
                    ngrn += regs;
                } else {
                    ngrn = 8;
                    nsaa = cfa64_align(nsaa, align >= 16 ? 16 : 8);
                    a->kind = K_STACK; a->stack = (uint32_t)nsaa; nsaa += cfa64_align(size, 8);
                }
            }
            break;

        case 't': case 'T':     /* 16-Byte-Ganzzahl: gerades Registerpaar */
            ngrn = (ngrn + 1) & ~1u;
            if (ngrn + 2 <= 8) {
                a->kind = K_GPR; a->reg = (uint8_t)ngrn; a->n = 2;
                a->offset = CFA64_X + 8 * a->reg;
                ngrn += 2;
            } else {
                ngrn = 8;
                nsaa = cfa64_align(nsaa, 16);
                a->kind = K_STACK; a->stack = (uint32_t)nsaa; nsaa += 16;
            }
            break;

        default:                /* Ganzzahlen, BOOL, Zeiger, Objekte, Selektoren, Blöcke */
            if (size == 0 || size > 8) size = 8;
            if (ngrn < 8) {
                a->kind = K_GPR; a->reg = (uint8_t)ngrn++; a->n = 1;
                a->offset = CFA64_X + 8 * a->reg;
                if (strchr("csCSB", *c) && size < 4) a->ext = *c;
            } else {
                nsaa = cfa64_align(nsaa, size);
                a->kind = K_STACK; a->stack = (uint32_t)nsaa; nsaa += size;
            }
            break;
        }
        if (a->kind == K_STACK) a->offset = (uint32_t)(CFA64_STACK + a->stack);
    }

    /* Schattenbereich hinter den Stack-Argumenten: float-HFA in Registern, Strukturen per Verweis */
    NSUInteger pos = CFA64_STACK + cfa64_align(nsaa, 16);
    for (NSUInteger i = 1; i < count; i++) {
        cfa64_arg *a = &out[i];
        if (a->kind == K_REF || (a->kind == K_HFA && a->msize == 4)) {
            pos = cfa64_align(pos, types[i].alignment ? types[i].alignment : 1);
            a->offset = (uint32_t)pos;
            pos += a->size;
        }
        if (set_offsets) types[i].offset = a->offset;
    }
    if (stacklen) *stacklen = cfa64_align(nsaa, 8);
    return cfa64_align(pos, 16);
}

/* --- Rahmen <-> Register ------------------------------------------------------------------- */
/* Weiterleitung: Rahmen aus den gesicherten Registern und dem Stack des Aufrufers füllen. */
static void cfa64_gather(const cfa64_arg *a, NSUInteger count, NSUInteger stacklen, const cfa64_saved *s,
                         unsigned char *frame) {
    memcpy(frame + CFA64_X, s->x, sizeof s->x);
    memcpy(frame + CFA64_X8, &s->x8, 8);
    for (int i = 0; i < 8; i++) memcpy(frame + CFA64_D + 8 * i, &s->q[i][0], 8);
    if (stacklen) memcpy(frame + CFA64_STACK, (const void *)s->stack, stacklen);
    for (NSUInteger i = 1; i < count; i++) {
        if (a[i].kind == K_HFA && a[i].msize == 4) {
            for (unsigned m = 0; m < a[i].n; m++) memcpy(frame + a[i].offset + 4 * m, &s->q[a[i].reg + m][0], 4);
        } else if (a[i].kind == K_REF) {
            const void *p = a[i].reg != CFA64_NOREG ? (const void *)s->x[a[i].reg]
                                                    : *(const void *const *)(s->stack + a[i].stack);
            memcpy(frame + a[i].offset, p, a[i].size);
        }
    }
}

/* Rückgabewert: Register -> Speicher (nach einem Aufruf) */
static void cfa64_ret_to_mem(const cfa64_retinfo *r, const cfa64_ret *rr, void *mem) {
    switch (r->kind) {
    case R_GPR: memcpy(mem, rr->x, r->size > 16 ? 16 : r->size); break;
    case R_FPR: memcpy(mem, rr->d, r->size > 32 ? 32 : r->size); break;
    case R_HFAF:
        for (unsigned m = 0; m < r->n; m++) memcpy((char *)mem + 4 * m, &rr->d[m], 4);
        break;
    default: break;
    }
}

/* Rückgabewert: Speicher -> Register (am Ende einer Weiterleitung) */
static void cfa64_mem_to_ret(const cfa64_retinfo *r, const void *mem, cfa64_ret *rr) {
    memset(rr, 0, sizeof *rr);
    switch (r->kind) {
    case R_GPR:
        memcpy(rr->x, mem, r->size > 16 ? 16 : r->size);
        switch (r->ext) {
        case 'c': rr->x[0] = (uint32_t)(int32_t)(int8_t)rr->x[0]; break;
        case 's': rr->x[0] = (uint32_t)(int32_t)(int16_t)rr->x[0]; break;
        default: break;   /* C, S, B: oben schon mit Nullen aufgefüllt */
        }
        break;
    case R_FPR: memcpy(rr->d, mem, r->size > 32 ? 32 : r->size); break;
    case R_HFAF:
        for (unsigned m = 0; m < r->n; m++) memcpy(&rr->d[m], (const char *)mem + 4 * m, 4);
        break;
    default: break;
    }
}

/* Aufruf von fn mit den Argumenten aus frame; Rückgabewert nach retdata (bei R_IND über x8). */
static void cfa64_invoke(NSMethodType *types, NSUInteger count, void *fn, void *retdata, const unsigned char *frame) {
    cfa64_arg *a = alloca(sizeof(cfa64_arg) * (count ? count : 1));
    NSUInteger stacklen;
    cfa64_retinfo r;
    cfa64_ret rr;
    unsigned char regs[CFA64_STACK];

    cfa64_layout(types, count, a, 0, &stacklen);
    cfa64_retclass(&types[0], &r);
    unsigned char *stack = alloca(stacklen + 16);
    memcpy(regs, frame, CFA64_STACK);
    memcpy(stack, frame + CFA64_STACK, stacklen);
    for (NSUInteger i = 1; i < count; i++) {
        if (a[i].kind == K_GPR && a[i].ext) {
            uint64_t *x = (uint64_t *)(regs + CFA64_X + 8 * a[i].reg);
            switch (a[i].ext) {
            case 'c': *x = (uint32_t)(int32_t)(int8_t)*x; break;
            case 's': *x = (uint32_t)(int32_t)(int16_t)*x; break;
            case 'S': *x = (uint16_t)*x; break;
            default:  *x = (uint8_t)*x; break;
            }
        } else if (a[i].kind == K_HFA && a[i].msize == 4) {
            for (unsigned m = 0; m < a[i].n; m++) {
                uint64_t v = 0;
                memcpy(&v, frame + a[i].offset + 4 * m, 4);
                memcpy(regs + CFA64_D + 8 * (a[i].reg + m), &v, 8);
            }
        } else if (a[i].kind == K_REF) {
            /* eigene Kopie wie beim Aufrufer: die Methode darf sie verändern, die Invocation
             * behält ihren Wert */
            void *copy = alloca(a[i].size + 16);
            copy = (void *)cfa64_align((NSUInteger)copy, 16);
            memcpy(copy, frame + a[i].offset, a[i].size);
            if (a[i].reg != CFA64_NOREG) memcpy(regs + CFA64_X + 8 * a[i].reg, &copy, 8);
            else memcpy(stack + a[i].stack, &copy, 8);
        }
    }
    void *x8 = r.kind == R_IND ? retdata : NULL;
    memcpy(regs + CFA64_X8, &x8, 8);
    __CFArm64Call(fn, regs, stack, stacklen, &rr);
    cfa64_ret_to_mem(&r, &rr, retdata);
}

#ifndef CLOKE_ARM64_TEST
/* --- Darling-Anbindung --------------------------------------------------------------------- */

/* aus NSMethodSignature.m (NSMethodSignature.m.sed): Offsets und Rahmenlänge für arm64 */
CFA64_HIDDEN void __CFArm64LayoutSignature(NSMethodType *types, NSUInteger count, NSUInteger *frameLength) {
    cfa64_arg *a = malloc(sizeof(cfa64_arg) * (count ? count : 1));
    *frameLength = cfa64_layout(types, count, a, 1, NULL);
    free(a);
}

static NSMethodType *cfa64_types(NSMethodSignature *sig, NSUInteger *count) {
    *count = [sig numberOfArguments] + 1;
    return [sig _argInfo:0];    /* die NSMethodType liegen als Feld hintereinander */
}

/* aus -[NSInvocation _invokeUsingIMP:withFrame:] (NSInvocation.m.sed) */
CFA64_HIDDEN void __CFArm64Invoke(NSMethodSignature *sig, IMP imp, void *retdata, void *frame) {
    NSUInteger count;
    NSMethodType *types = cfa64_types(sig, &count);
    cfa64_invoke(types, count, (void *)imp, retdata, frame);
}

/* Rahmen für die gesicherten Register bauen und daraus eine Invocation machen */
static id cfa64_invocation(Class cls, NSMethodSignature *sig, const cfa64_saved *s) {
    NSUInteger count, stacklen;
    NSMethodType *types = cfa64_types(sig, &count);
    cfa64_arg *a = alloca(sizeof(cfa64_arg) * count);
    NSUInteger flen = cfa64_layout(types, count, a, 0, &stacklen);
    unsigned char *frame = calloc(1, flen > [sig frameLength] ? flen : [sig frameLength]);
    cfa64_gather(a, count, stacklen, s, frame);
    id inv = [cls _invocationWithMethodSignature:sig frame:frame];
    free(frame);
    return inv;
}

static void cfa64_return(NSMethodSignature *sig, NSInvocation *inv, const cfa64_saved *s, cfa64_ret *rr) {
    NSUInteger count;
    NSMethodType *types = cfa64_types(sig, &count);
    cfa64_retinfo r;
    cfa64_retclass(&types[0], &r);
    memset(rr, 0, sizeof *rr);
    if (r.kind == R_VOID) return;
    if (r.kind == R_IND) {
        [inv getReturnValue:(void *)s->x8];
        return;
    }
    unsigned char buf[64] = {0};
    [inv getReturnValue:buf];
    cfa64_mem_to_ret(&r, buf, rr);
}

/* Wie ___forwarding___ in NSForwarding.m: gibt nil zurück, wenn erledigt (Rückgabewert in *rr),
 * sonst das neue Ziel, an das __CF_forwarding_prep_0 die Nachricht neu sendet. */
CFA64_HIDDEN id __CFArm64Forwarding(cfa64_saved *s, cfa64_ret *rr) {
    id self = (id)s->x[0];
    SEL _cmd = (SEL)s->x[1];
    Class class = object_getClass(self);
    const char *className = class_getName(class);

    memset(rr, 0, sizeof *rr);
    if (strncmp(className, ZOMBIE_PREFIX, strlen(ZOMBIE_PREFIX)) == 0) {
        printf("-[%s %s] message sent to deallocated instance %p.\n", className + strlen(ZOMBIE_PREFIX),
               sel_getName(_cmd), self);
        __builtin_trap();
    }

    if (class_respondsToSelector(class, @selector(forwardingTargetForSelector:))) {
        id target = [self forwardingTargetForSelector:_cmd];
        if (target != nil && target != self) return target;
    }

    NSMethodSignature *sig = [self methodSignatureForSelector:_cmd];
    if (sig == nil) {
        [self doesNotRecognizeSelector:_cmd];
        return self;
    }
    NSUInteger colons = 2;
    for (const char *p = sel_getName(_cmd); *p; p++) colons += *p == ':';
    if (colons != [sig numberOfArguments])
        printf("NSForwardSignatureError: invoked with %lu args, but %lu expected. Selector %s, class %s\n",
               (unsigned long)colons, (unsigned long)[sig numberOfArguments], sel_getName(_cmd), className);

    NSInvocation *inv = cfa64_invocation([NSInvocation class], sig, s);
    [self forwardInvocation:inv];
    cfa64_return(sig, inv, s, rr);
    return nil;
}

/* Wie __block_forwarding__: Darlings Weiterleitungs-Blöcke (__NSMakeSpecialForwardingCaptureBlock),
 * hier mit Rückgabewert. */
CFA64_HIDDEN void __CFArm64BlockForwarding(cfa64_saved *s, cfa64_ret *rr) {
    id block = (id)s->x[0];
    const char *className = class_getName(object_getClass(block));

    memset(rr, 0, sizeof *rr);
    if (strncmp(className, ZOMBIE_PREFIX, strlen(ZOMBIE_PREFIX)) == 0) {
        CFLog(3, CFSTR("*** NSBlockInvocation: invocation of deallocated Block instance %p"), block);
        __builtin_trap();
    }
    const char *rawSig = _Block_signature(block);
    if (!rawSig) {
        CFLog(4, CFSTR("*** NSBlockInvocation: Block %p does not have a type signature -- abort"), block);
        __builtin_trap();
    }
    NSMethodSignature *sig = [NSMethodSignature signatureWithObjCTypes:rawSig];
    NSBlockInvocation *inv = cfa64_invocation([NSBlockInvocation class], sig, s);
    inv.target = nil;   /* damit der Proxy nicht versehentlich wieder hier landet */
    (((struct NSProxyBlock *)block)->proxy)(inv);
    cfa64_return(sig, inv, s, rr);
}
#endif /* CLOKE_ARM64_TEST */

#endif /* __arm64__ */
