// PureDarwins libSystem hat keine komplexe Mathematik: die vom Compiler für Z * Z und Z / Z erzeugten
// Hilfsfunktionen und cabs/csqrt (ohne Behandlung von Unendlich/NaN, die LAPACK-Kern hier nicht braucht).
#include <math.h>
#define HID __attribute__((visibility("hidden")))
HID double _Complex __muldc3(double a, double b, double c, double d) {
  return __builtin_complex(a * c - b * d, a * d + b * c);
}
HID double _Complex __divdc3(double a, double b, double c, double d) {
  double den = c * c + d * d;
  return __builtin_complex((a * c + b * d) / den, (b * c - a * d) / den);
}
HID double acc_cabs(double _Complex z) { return hypot(__real__ z, __imag__ z); }
HID double _Complex acc_csqrt(double _Complex z) {
  double x = __real__ z, y = __imag__ z, m = hypot(x, y);
  double r = sqrt((m + fabs(x)) / 2), i = r == 0 ? 0 : fabs(y) / (2 * r);
  if (x >= 0) return __builtin_complex(r, y < 0 ? -i : i);
  return __builtin_complex(i, y < 0 ? -r : r);
}
