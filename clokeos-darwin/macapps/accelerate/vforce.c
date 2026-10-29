// Accelerate.framework für ClokeOS: vForce (vv*-Funktionen über libm, schleifenvektorisiert) und vfp.
#include <Accelerate/Accelerate.h>
#include <math.h>

#define V1F(name, fn) void name(float *y, const float *x, const int *n) { for (int i = 0; i < *n; i++) y[i] = fn(x[i]); }
#define V1D(name, fn) void name(double *y, const double *x, const int *n) { for (int i = 0; i < *n; i++) y[i] = fn(x[i]); }
#define V2F(name, fn) void name(float *z, const float *y, const float *x, const int *n) { for (int i = 0; i < *n; i++) z[i] = fn(y[i], x[i]); }
#define V2D(name, fn) void name(double *z, const double *y, const double *x, const int *n) { for (int i = 0; i < *n; i++) z[i] = fn(y[i], x[i]); }

V1F(vvexpf, expf) V1D(vvexp, exp) V1F(vvexp2f, exp2f) V1D(vvexp2, exp2) V1F(vvexpm1f, expm1f) V1D(vvexpm1, expm1)
V1F(vvlogf, logf) V1D(vvlog, log) V1F(vvlog2f, log2f) V1D(vvlog2, log2) V1F(vvlog10f, log10f) V1D(vvlog10, log10)
V1F(vvlog1pf, log1pf) V1D(vvlog1p, log1p)
V1F(vvsqrtf, sqrtf) V1D(vvsqrt, sqrt) V1F(vvcbrtf, cbrtf) V1D(vvcbrt, cbrt)
V1F(vvfabsf, fabsf) V1D(vvfabs, fabs) V1F(vvfloorf, floorf) V1D(vvfloor, floor) V1F(vvceilf, ceilf) V1D(vvceil, ceil)
V1F(vvnintf, roundf) V1D(vvnint, round) V1F(vvintf, truncf) V1D(vvint, trunc)
V1F(vvsinf, sinf) V1D(vvsin, sin) V1F(vvcosf, cosf) V1D(vvcos, cos) V1F(vvtanf, tanf) V1D(vvtan, tan)
V1F(vvasinf, asinf) V1D(vvasin, asin) V1F(vvacosf, acosf) V1D(vvacos, acos) V1F(vvatanf, atanf) V1D(vvatan, atan)
V1F(vvsinhf, sinhf) V1D(vvsinh, sinh) V1F(vvcoshf, coshf) V1D(vvcosh, cosh) V1F(vvtanhf, tanhf) V1D(vvtanh, tanh)
V1F(vvasinhf, asinhf) V1D(vvasinh, asinh) V1F(vvacoshf, acoshf) V1D(vvacosh, acosh) V1F(vvatanhf, atanhf) V1D(vvatanh, atanh)
V1F(vvrecf, 1.0f /) V1D(vvrec, 1.0 /)
V2F(vvfmodf, fmodf) V2D(vvfmod, fmod) V2F(vvpowf, powf) V2D(vvpow, pow) V2F(vvatan2f, atan2f) V2D(vvatan2, atan2)
V2F(vvremainderf, remainderf) V2D(vvremainder, remainder) V2F(vvcopysignf, copysignf) V2D(vvcopysign, copysign)
void vvdivf(float *z, const float *y, const float *x, const int *n) { for (int i = 0; i < *n; i++) z[i] = y[i] / x[i]; }
void vvdiv(double *z, const double *y, const double *x, const int *n) { for (int i = 0; i < *n; i++) z[i] = y[i] / x[i]; }
void vvsincosf(float *s, float *c, const float *x, const int *n) { for (int i = 0; i < *n; i++) { s[i] = sinf(x[i]); c[i] = cosf(x[i]); } }
void vvsincos(double *s, double *c, const double *x, const int *n) { for (int i = 0; i < *n; i++) { s[i] = sin(x[i]); c[i] = cos(x[i]); } }

// vfp.h: Vektor aus vier floats (vFloat)
vFloat vfabsf(vFloat v) { return (vFloat){fabsf(v[0]), fabsf(v[1]), fabsf(v[2]), fabsf(v[3])}; }
