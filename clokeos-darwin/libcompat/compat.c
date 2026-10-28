/* libclokeos_compat: Symbole, die Programme aus Apples SDK an libSystem binden, PureDarwins
 * libSystem (20.5) aber nicht exportiert. libcompat/build.sh trägt die Bibliothek als
 * zusätzliches LC_REEXPORT_DYLIB in PureDarwins /usr/lib/libSystem.B.dylib ein; dyld findet die
 * Symbole dann über libSystem, auch bei zweistufig gebundenen Programmen.
 *
 * Die Bibliothek hängt von nichts ab (keine Importe, auch nicht von libSystem) und exportiert je
 * Architektur nur, was dort fehlt (build.sh prüft das):
 *
 *   arm64   ___chkstk_darwin (chkstk.S), die fenv-Funktionen mit _FE_DFL_ENV und
 *           _FE_DFL_DISABLE_DENORMS_ENV, nexttoward, nexttowardf, nexttowardl, scalbnl
 *   x86_64  ___chkstk_darwin (clang ruft auf x86_64 ____chkstk_darwin, das hat PureDarwin),
 *           der fehlende Teil der fenv-Funktionen (fegetround, fesetround, feraiseexcept,
 *           fegetexceptflag, feupdateenv) mit _FE_DFL_ENV und _FE_DFL_DISABLE_SSE_DENORMS_ENV
 *
 * Verhalten wie Apples libm (macOS 27 auf dem Mac verglichen, siehe compattest.c): auf arm64 hebt
 * feraiseexcept mit Overflow und Underflow auch Inexact an, ein ungültiger Rundungsmodus lässt
 * fesetround nichts ändern und gibt den Wert zurück. long double ist auf arm64 dasselbe wie
 * double. */
#include <fenv.h>
#include <math.h>
#include <stdint.h>

#if defined(__arm64__)

#define FE_ALL_ROUND (FE_TONEAREST | FE_UPWARD | FE_DOWNWARD | FE_TOWARDZERO)
#define FPCR_TRAPS (__fpcr_trap_invalid | __fpcr_trap_divbyzero | __fpcr_trap_overflow | \
                    __fpcr_trap_underflow | __fpcr_trap_inexact | __fpcr_trap_denormal)

static inline uint64_t get_fpsr(void) { uint64_t v; __asm__ volatile("mrs %0, fpsr" : "=r"(v)); return v; }
static inline uint64_t get_fpcr(void) { uint64_t v; __asm__ volatile("mrs %0, fpcr" : "=r"(v)); return v; }
static inline void set_fpsr(uint64_t v) { __asm__ volatile("msr fpsr, %0" : : "r"(v)); }
static inline void set_fpcr(uint64_t v) { __asm__ volatile("msr fpcr, %0" : : "r"(v)); }

const fenv_t _FE_DFL_ENV = {0, 0};
const fenv_t _FE_DFL_DISABLE_DENORMS_ENV = {0, __fpcr_flush_to_zero};

/* Die IEEE-Ausnahmen durch Rechnen auslösen, damit eingeschaltete Traps wirklich auslösen;
 * FE_FLUSHTOZERO (Eingabe-Denormal) lässt sich so nicht erzeugen und kommt direkt ins FPSR. */
static void raise_excepts(int excepts) {
  volatile double a, r;
  if (excepts & FE_INVALID) { a = 0.0; r = a / a; }
  if (excepts & FE_DIVBYZERO) { a = 0.0; r = 1.0 / a; }
  if (excepts & FE_OVERFLOW) { a = 0x1p1023; r = a * a; }
  if (excepts & FE_UNDERFLOW) { a = 0x1p-1022; r = a * a; }
  if (excepts & FE_INEXACT) { a = 0x1p-1022; r = 1.0 + a; }
  if (excepts & FE_FLUSHTOZERO) set_fpsr(get_fpsr() | FE_FLUSHTOZERO);
  (void)r;
}

int feclearexcept(int excepts) {
  set_fpsr(get_fpsr() & ~(uint64_t)(excepts & FE_ALL_EXCEPT));
  return 0;
}

int fegetexceptflag(fexcept_t *flagp, int excepts) {
  *flagp = (fexcept_t)(get_fpsr() & (uint64_t)(excepts & FE_ALL_EXCEPT));
  return 0;
}

int feraiseexcept(int excepts) {
  raise_excepts(excepts & FE_ALL_EXCEPT);
  return 0;
}

int fesetexceptflag(const fexcept_t *flagp, int excepts) {
  uint64_t m = (uint64_t)(excepts & FE_ALL_EXCEPT);
  set_fpsr((get_fpsr() & ~m) | (*flagp & m));
  return 0;
}

int fetestexcept(int excepts) {
  return (int)(get_fpsr() & (uint64_t)(excepts & FE_ALL_EXCEPT));
}

int fegetround(void) {
  return (int)(get_fpcr() & FE_ALL_ROUND);
}

int fesetround(int round) {
  if (round & ~FE_ALL_ROUND) return round;
  set_fpcr((get_fpcr() & ~(uint64_t)FE_ALL_ROUND) | (uint64_t)round);
  return 0;
}

int fegetenv(fenv_t *envp) {
  envp->__fpsr = get_fpsr();
  envp->__fpcr = get_fpcr();
  return 0;
}

int feholdexcept(fenv_t *envp) {
  fegetenv(envp);
  set_fpsr(envp->__fpsr & ~(uint64_t)FE_ALL_EXCEPT);
  set_fpcr(envp->__fpcr & ~(uint64_t)FPCR_TRAPS);
  return 0;
}

int fesetenv(const fenv_t *envp) {
  set_fpsr(envp->__fpsr);
  set_fpcr(envp->__fpcr);
  return 0;
}

int feupdateenv(const fenv_t *envp) {
  int pending = (int)(get_fpsr() & FE_ALL_EXCEPT);
  fesetenv(envp);
  raise_excepts(pending);
  return 0;
}

/* nextafter in Richtung eines double (wie musl): Über- und Unterlauf lösen ihre Ausnahmen aus */
static double next_double(double x, double y) {
  union { double f; uint64_t i; } u = {x};
  volatile double t;
  if (__builtin_isnan(x) || __builtin_isnan(y)) return x + y;
  if (x == y) return y;
  if (x == 0) {
    u.i = 1;
    if (__builtin_signbit(y)) u.i |= 1ULL << 63;
  } else if ((x < y) == !(u.i >> 63)) {
    u.i++;
  } else {
    u.i--;
  }
  uint64_t e = u.i >> 52 & 0x7ff;
  if (e == 0x7ff) t = x + x;
  if (e == 0) t = x * x + u.f * u.f;
  (void)t;
  return u.f;
}

double nexttoward(double x, long double y) { return next_double(x, (double)y); }
long double nexttowardl(long double x, long double y) { return next_double((double)x, (double)y); }

float nexttowardf(float x, long double y) {
  union { float f; uint32_t i; } u = {x};
  volatile float t;
  double yd = (double)y;
  if (__builtin_isnan(x) || __builtin_isnan(yd)) return (float)(x + yd);
  if ((double)x == yd) return (float)yd;
  if (x == 0) {
    u.i = 1;
    if (__builtin_signbit(yd)) u.i |= 0x80000000u;
  } else if ((double)x < yd) {
    if (__builtin_signbit(x)) u.i--; else u.i++;
  } else {
    if (__builtin_signbit(x)) u.i++; else u.i--;
  }
  uint32_t e = u.i & 0x7f800000u;
  if (e == 0x7f800000u) t = x + x;
  if (e == 0) t = x * x + u.f * u.f;
  (void)t;
  return u.f;
}

/* scalbn wie musl; bei Unterlauf ohne doppelte Rundung */
long double scalbnl(long double xl, int n) {
  union { double f; uint64_t i; } u;
  double y = (double)xl;
  if (n > 1023) {
    y *= 0x1p1023;
    n -= 1023;
    if (n > 1023) {
      y *= 0x1p1023;
      n -= 1023;
      if (n > 1023) n = 1023;
    }
  } else if (n < -1022) {
    y *= 0x1p-1022 * 0x1p53;
    n += 1022 - 53;
    if (n < -1022) {
      y *= 0x1p-1022 * 0x1p53;
      n += 1022 - 53;
      if (n < -1022) n = -1022;
    }
  }
  u.i = (uint64_t)(0x3ff + n) << 52;
  return y * u.f;
}

#elif defined(__x86_64__)

/* fenv_t wie bei Apple und PureDarwin: x87-Steuer- und Statuswort, MXCSR, 8 Bytes Reserve */
#define FE_ALL_ROUND (FE_TONEAREST | FE_DOWNWARD | FE_UPWARD | FE_TOWARDZERO)

struct x87_env {      /* fnstenv/fldenv im 32-Bit-Format */
  uint16_t control, r0, status, r1, tag, r2;
  uint32_t ip;
  uint16_t cs, opcode;
  uint32_t dp;
  uint16_t ds, r3;
};

static inline uint32_t get_mxcsr(void) { uint32_t v; __asm__ volatile("stmxcsr %0" : "=m"(v)); return v; }
static inline void set_mxcsr(uint32_t v) { __asm__ volatile("ldmxcsr %0" : : "m"(v)); }
static inline uint16_t get_x87cw(void) { uint16_t v; __asm__ volatile("fnstcw %0" : "=m"(v)); return v; }
static inline void set_x87cw(uint16_t v) { __asm__ volatile("fldcw %0" : : "m"(v)); }
static inline uint16_t get_x87sw(void) { uint16_t v; __asm__ volatile("fnstsw %0" : "=m"(v)); return v; }

const fenv_t _FE_DFL_ENV = {0x037f, 0x0000, 0x00001f80, {0}};
const fenv_t _FE_DFL_DISABLE_SSE_DENORMS_ENV = {0x037f, 0x0000, 0x00009fc0, {0}};

/* wie PureDarwins (nicht exportiertes) feraiseexcept: Flags in x87 und SSE setzen, dann fwait,
 * damit eine nicht maskierte x87-Ausnahme sofort auslöst */
static void raise_excepts(int excepts) {
  struct x87_env env;
  uint16_t m = (uint16_t)(excepts & FE_ALL_EXCEPT);
  __asm__ volatile("fnstenv %0" : "=m"(env));
  env.status |= m;
  __asm__ volatile("fldenv %0" : : "m"(env));
  set_mxcsr(get_mxcsr() | m);
  __asm__ volatile("fwait");
}

int fegetexceptflag(fexcept_t *flagp, int excepts) {
  *flagp = (fexcept_t)((get_x87sw() | get_mxcsr()) & (unsigned)(excepts & FE_ALL_EXCEPT));
  return 0;
}

int feraiseexcept(int excepts) {
  raise_excepts(excepts);
  return 0;
}

int fegetround(void) {
  return get_x87cw() & FE_ALL_ROUND;
}

int fesetround(int round) {
  if (round & ~FE_ALL_ROUND) return round;
  set_x87cw((uint16_t)((get_x87cw() & ~FE_ALL_ROUND) | round));
  set_mxcsr((get_mxcsr() & ~(FE_ALL_ROUND << 3)) | ((uint32_t)round << 3));
  return 0;
}

int feupdateenv(const fenv_t *envp) {
  int pending = (get_x87sw() | get_mxcsr()) & FE_ALL_EXCEPT;
  struct x87_env env;
  __asm__ volatile("fnstenv %0" : "=m"(env));
  env.control = envp->__control;
  env.status = envp->__status;
  set_mxcsr(envp->__mxcsr);
  __asm__ volatile("fldenv %0" : : "m"(env));
  raise_excepts(pending);
  return 0;
}

#else
#error Architektur nicht unterstützt
#endif
