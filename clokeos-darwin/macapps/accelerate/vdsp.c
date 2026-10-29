// Accelerate.framework für ClokeOS: vDSP. Eigener Code nach den dokumentierten Bedeutungen der
// Funktionen (SDK-Header geprüft, indem diese Datei gegen <Accelerate/Accelerate.h> übersetzt wird).
#include <Accelerate/Accelerate.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#define CAT_(a, b) a##b
#define CAT(a, b) CAT_(a, b)

#define T float
#define ZT DSPSplitComplex
#define CT DSPComplex
#define SUF
#define FABS fabsf
#define SQRT sqrtf
#define LOG10 log10f
#define TRUNC truncf
#define ATAN2 atan2f
#define COS cosf
#define SIN sinf
#define INFTY INFINITY
#include "vdsp_t.h"
#include "vdsp_c.h"
#undef T
#undef ZT
#undef CT
#undef SUF
#undef FABS
#undef SQRT
#undef LOG10
#undef TRUNC
#undef ATAN2
#undef COS
#undef SIN
#undef INFTY
#undef FN
#undef ZFN

#define T double
#define ZT DSPDoubleSplitComplex
#define CT DSPDoubleComplex
#define SUF D
#define FABS fabs
#define SQRT sqrt
#define LOG10 log10
#define TRUNC trunc
#define ATAN2 atan2
#define COS cos
#define SIN sin
#define INFTY INFINITY
#include "vdsp_t.h"
#include "vdsp_c.h"

// ---- Fenster ----
static void window(float *C, vDSP_Length N, int flags, int kind) {
  vDSP_Length n = (flags & vDSP_HALF_WINDOW) ? (N + 1) / 2 : N;
  double nd = (double)N;
  for (vDSP_Length i = 0; i < n; i++) {
    double a = 2.0 * M_PI * (double)i / nd, w;
    switch (kind) {
      case 0: w = 0.42 - 0.5 * cos(a) + 0.08 * cos(2 * a); break;
      case 1: w = 0.54 - 0.46 * cos(a); break;
      default: w = 0.5 * (1.0 - cos(a)); if (flags & vDSP_HANN_NORM) w *= sqrt(8.0 / 3.0); break;
    }
    C[i] = (float)w;
  }
}
void vDSP_blkman_window(float *C, vDSP_Length N, int flags) { window(C, N, flags, 0); }
void vDSP_hamm_window(float *C, vDSP_Length N, int flags) { window(C, N, flags, 1); }
void vDSP_hann_window(float *C, vDSP_Length N, int flags) { window(C, N, flags, 2); }
void vDSP_hann_windowD(double *C, vDSP_Length N, int flags) {
  vDSP_Length n = (flags & vDSP_HALF_WINDOW) ? (N + 1) / 2 : N;
  for (vDSP_Length i = 0; i < n; i++) {
    double w = 0.5 * (1.0 - cos(2.0 * M_PI * (double)i / (double)N));
    C[i] = (flags & vDSP_HANN_NORM) ? w * sqrt(8.0 / 3.0) : w;
  }
}

// ---- Umwandlung Ganzzahl <-> float ----
void vDSP_vflt8(const char *A, vDSP_Stride IA, float *C, vDSP_Stride IC, vDSP_Length N) { LOOP C[n * IC] = (float)A[n * IA]; }
void vDSP_vflt16(const short *A, vDSP_Stride IA, float *C, vDSP_Stride IC, vDSP_Length N) { LOOP C[n * IC] = (float)A[n * IA]; }
void vDSP_vflt32(const int *A, vDSP_Stride IA, float *C, vDSP_Stride IC, vDSP_Length N) { LOOP C[n * IC] = (float)A[n * IA]; }
static inline long long satl(float x, long long lo, long long hi) {
  if (x != x) return 0;
  if (x <= (float)lo) return lo;
  if (x >= (float)hi) return hi;
  return (long long)x;
}
void vDSP_vfix16(const float *A, vDSP_Stride IA, short *C, vDSP_Stride IC, vDSP_Length N) { LOOP C[n * IC] = (short)satl(truncf(A[n * IA]), -32768, 32767); }
void vDSP_vfix32(const float *A, vDSP_Stride IA, int *C, vDSP_Stride IC, vDSP_Length N) { LOOP C[n * IC] = (int)satl(truncf(A[n * IA]), INT32_MIN, INT32_MAX); }
void vDSP_vfixr16(const float *A, vDSP_Stride IA, short *C, vDSP_Stride IC, vDSP_Length N) { LOOP C[n * IC] = (short)satl(rintf(A[n * IA]), -32768, 32767); }
void vDSP_vfixr32(const float *A, vDSP_Stride IA, int *C, vDSP_Stride IC, vDSP_Length N) { LOOP C[n * IC] = (int)satl(rintf(A[n * IA]), INT32_MIN, INT32_MAX); }

// ---- Rampen mit Multiplikation ----
void vDSP_vrampmul(const float *I, vDSP_Stride IS, float *A, const float *B, float *O, vDSP_Stride OS, vDSP_Length N) {
  float a = *A, b = *B; LOOP O[n * OS] = I[n * IS] * (a + (float)n * b); *A = a + (float)N * b;
}
void vDSP_vrampmuladd(const float *I, vDSP_Stride IS, float *A, const float *B, float *O, vDSP_Stride OS, vDSP_Length N) {
  float a = *A, b = *B; LOOP O[n * OS] += I[n * IS] * (a + (float)n * b); *A = a + (float)N * b;
}
void vDSP_vrampmul2(const float *I0, const float *I1, vDSP_Stride IS, float *A, const float *B, float *O0, float *O1, vDSP_Stride OS, vDSP_Length N) {
  float a = *A, b = *B;
  LOOP { float g = a + (float)n * b; O0[n * OS] = I0[n * IS] * g; O1[n * OS] = I1[n * IS] * g; }
  *A = a + (float)N * b;
}

// ---- Interpolation ----
void vDSP_vlint(const float *A, const float *B, vDSP_Stride IB, float *C, vDSP_Stride IC, vDSP_Length N, vDSP_Length M) {
  LOOP { float p = B[n * IB]; long j = (long)p; float f = p - (float)j; C[n * IC] = A[j] + f * (A[j + 1] - A[j]); }
}
void vDSP_vqint(const float *A, const float *B, vDSP_Stride IB, float *C, vDSP_Stride IC, vDSP_Length N, vDSP_Length M) {
  LOOP {
    float p = B[n * IB]; long j = (long)p; float b = p - (float)j;
    float c = A[j], d = A[j + 1], e = A[j + 2];
    C[n * IC] = d + 0.5f * b * ((e - c) + b * (e - 2 * d + c));
  }
}
// Tabelle C (M Werte), Position = A*S1 + S2, auf [0, M-1] begrenzt, linear interpoliert
void vDSP_vtabi(const float *A, vDSP_Stride IA, const float *S1, const float *S2, const float *C, vDSP_Length M, float *D, vDSP_Stride ID, vDSP_Length N) {
  float s1 = *S1, s2 = *S2, top = (float)(M - 1);
  LOOP {
    float p = A[n * IA] * s1 + s2;
    if (p < 0) p = 0; if (p > top) p = top;
    long j = (long)p; float f = p - (float)j;
    D[n * ID] = (j + 1 < (long)M) ? C[j] + f * (C[j + 1] - C[j]) : C[j];
  }
}
// stückweise lineare Erzeugung: Stützwerte A[m] an Positionen B[m] (aufsteigend, ganze Zahlen)
void vDSP_vgenp(const float *A, vDSP_Stride IA, const float *B, vDSP_Stride IB, float *C, vDSP_Stride IC, vDSP_Length N, vDSP_Length M) {
  vDSP_Length k = 0;
  LOOP {
    while (k + 2 < M && (float)n >= B[(k + 1) * IB]) k++;
    float x0 = B[k * IB], x1 = B[(k + 1) * IB], y0 = A[k * IA], y1 = A[(k + 1) * IA];
    C[n * IC] = x1 == x0 ? y0 : y0 + (y1 - y0) * ((float)n - x0) / (x1 - x0);
  }
}

// ---- Sortieren (Order: 1 aufsteigend, -1 absteigend) ----
static int cmpf_up(const void *a, const void *b) { float x = *(const float *)a, y = *(const float *)b; return (x > y) - (x < y); }
static int cmpf_dn(const void *a, const void *b) { return cmpf_up(b, a); }
static int cmpd_up(const void *a, const void *b) { double x = *(const double *)a, y = *(const double *)b; return (x > y) - (x < y); }
static int cmpd_dn(const void *a, const void *b) { return cmpd_up(b, a); }
void vDSP_vsort(float *C, vDSP_Length N, int Order) { qsort(C, N, sizeof(float), Order >= 0 ? cmpf_up : cmpf_dn); }
void vDSP_vsortD(double *C, vDSP_Length N, int Order) { qsort(C, N, sizeof(double), Order >= 0 ? cmpd_up : cmpd_dn); }
static void isort(const float *C, vDSP_Length *I, vDSP_Length *tmp, vDSP_Length N, int up) {
  // stabile Mischsortierung der Indexliste I nach C[I[k]]
  vDSP_Length *t = tmp ? tmp : malloc(N * sizeof *t);
  for (vDSP_Length w = 1; w < N; w *= 2) {
    for (vDSP_Length lo = 0; lo < N; lo += 2 * w) {
      vDSP_Length mid = lo + w < N ? lo + w : N, hi = lo + 2 * w < N ? lo + 2 * w : N, i = lo, j = mid, k = lo;
      while (i < mid && j < hi) {
        float a = C[I[i]], b = C[I[j]];
        t[k++] = (up ? (b < a) : (b > a)) ? I[j++] : I[i++];
      }
      while (i < mid) t[k++] = I[i++];
      while (j < hi) t[k++] = I[j++];
    }
    memcpy(I, t, N * sizeof *I);
  }
  if (!tmp) free(t);
}
void vDSP_vsorti(const float *C, vDSP_Length *I, vDSP_Length *Temporary, vDSP_Length N, int Order) { isort(C, I, Temporary, N, Order >= 0); }

// ---- Nulldurchgänge: B = höchste gesuchte Anzahl; C = Index des letzten gefundenen, D = Anzahl ----
void vDSP_nzcros(const float *A, vDSP_Stride IA, vDSP_Length B, vDSP_Length *C, vDSP_Length *D, vDSP_Length N) {
  vDSP_Length cnt = 0, last = 0;
  for (vDSP_Length n = 1; n < N && cnt < B; n++) {
    float p = A[(n - 1) * IA], q = A[n * IA];
    if ((p < 0 && q >= 0) || (p > 0 && q <= 0)) { cnt++; last = n; }
  }
  *C = last; *D = cnt;
}

// ---- Bild-FIR (double): Korrelation mit P x Q Filter, Rand mit Nullen aufgefüllt ----
void vDSP_imgfirD(const double *A, vDSP_Length NR, vDSP_Length NC, const double *F, double *C, vDSP_Length P, vDSP_Length Q) {
  long hp = (long)P / 2, hq = (long)Q / 2;
  for (long r = 0; r < (long)NR; r++)
    for (long c = 0; c < (long)NC; c++) {
      double s = 0;
      for (long p = 0; p < (long)P; p++) {
        long rr = r + p - hp; if (rr < 0 || rr >= (long)NR) continue;
        for (long q = 0; q < (long)Q; q++) {
          long cc = c + q - hq; if (cc < 0 || cc >= (long)NC) continue;
          s += A[rr * NC + cc] * F[p * Q + q];
        }
      }
      C[r * NC + c] = s;
    }
}

// ---- Biquad-Kaskade (Direktform II transponiert; Verzögerung 2 Werte je Abschnitt) ----
struct vDSP_biquad_SetupStruct { vDSP_Length M; double c[]; };
vDSP_biquad_Setup vDSP_biquad_CreateSetup(const double *coeffs, vDSP_Length M) {
  struct vDSP_biquad_SetupStruct *s = malloc(sizeof *s + 5 * M * sizeof(double));
  if (!s) return NULL;
  s->M = M; memcpy(s->c, coeffs, 5 * M * sizeof(double));
  return (vDSP_biquad_Setup)s;
}
void vDSP_biquad_DestroySetup(vDSP_biquad_Setup setup) { free((void *)setup); }
void vDSP_biquad(const struct vDSP_biquad_SetupStruct *s, float *Delay, const float *X, vDSP_Stride IX, float *Y, vDSP_Stride IY, vDSP_Length N) {
  for (vDSP_Length m = 0; m < s->M; m++) {
    const double *k = s->c + 5 * m;
    float b0 = (float)k[0], b1 = (float)k[1], b2 = (float)k[2], a1 = (float)k[3], a2 = (float)k[4];
    float z1 = Delay[2 * m], z2 = Delay[2 * m + 1];
    const float *in = m == 0 ? X : Y; vDSP_Stride is = m == 0 ? IX : IY;
    for (vDSP_Length n = 0; n < N; n++) {
      float x = in[n * is], y = b0 * x + z1;
      z1 = b1 * x - a1 * y + z2; z2 = b2 * x - a2 * y;
      Y[n * IY] = y;
    }
    Delay[2 * m] = z1; Delay[2 * m + 1] = z2;
  }
}
