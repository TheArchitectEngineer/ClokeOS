// Komplexe vDSP-Funktionen (Split-Complex) je Genauigkeit; Gegenstück zu vdsp_t.h.
#define ZFN(x) CAT(vDSP_##x, SUF)

void ZFN(ctoz)(const CT *C, vDSP_Stride IC, const ZT *Z, vDSP_Stride IZ, vDSP_Length N) {
  const T *c = (const T *)C;
  LOOP { Z->realp[n * IZ] = c[n * IC]; Z->imagp[n * IZ] = c[n * IC + 1]; }
}
void ZFN(ztoc)(const ZT *Z, vDSP_Stride IZ, CT *C, vDSP_Stride IC, vDSP_Length N) {
  T *c = (T *)C;
  LOOP { c[n * IC] = Z->realp[n * IZ]; c[n * IC + 1] = Z->imagp[n * IZ]; }
}
void ZFN(polar)(const T *A, vDSP_Stride IA, T *C, vDSP_Stride IC, vDSP_Length N) {
  LOOP { T x = A[n * IA], y = A[n * IA + 1]; C[n * IC] = SQRT(x * x + y * y); C[n * IC + 1] = ATAN2(y, x); }
}
void ZFN(rect)(const T *A, vDSP_Stride IA, T *C, vDSP_Stride IC, vDSP_Length N) {
  LOOP { T r = A[n * IA], p = A[n * IA + 1]; C[n * IC] = r * COS(p); C[n * IC + 1] = r * SIN(p); }
}
void ZFN(zvadd)(const ZT *A, vDSP_Stride IA, const ZT *B, vDSP_Stride IB, const ZT *C, vDSP_Stride IC, vDSP_Length N) {
  LOOP { C->realp[n * IC] = A->realp[n * IA] + B->realp[n * IB]; C->imagp[n * IC] = A->imagp[n * IA] + B->imagp[n * IB]; }
}
// Conjugate = 1: A*B, -1: conj(A)*B
void ZFN(zvmul)(const ZT *A, vDSP_Stride IA, const ZT *B, vDSP_Stride IB, const ZT *C, vDSP_Stride IC, vDSP_Length N, int Conjugate) {
  T s = Conjugate < 0 ? -1 : 1;
  LOOP {
    T ar = A->realp[n * IA], ai = s * A->imagp[n * IA], br = B->realp[n * IB], bi = B->imagp[n * IB];
    C->realp[n * IC] = ar * br - ai * bi; C->imagp[n * IC] = ar * bi + ai * br;
  }
}
// C = conj(A) * B? Bedeutung wird gegen Apple geprüft (test)
void ZFN(zvcmul)(const ZT *A, vDSP_Stride IA, const ZT *B, vDSP_Stride IB, const ZT *C, vDSP_Stride IC, vDSP_Length N) {
  LOOP {
    T ar = A->realp[n * IA], ai = -A->imagp[n * IA], br = B->realp[n * IB], bi = B->imagp[n * IB];
    C->realp[n * IC] = ar * br - ai * bi; C->imagp[n * IC] = ar * bi + ai * br;
  }
}
// D = conj(A)*B + C
void ZFN(zvcma)(const ZT *A, vDSP_Stride IA, const ZT *B, vDSP_Stride IB, const ZT *C, vDSP_Stride IC, const ZT *D, vDSP_Stride ID, vDSP_Length N) {
  LOOP {
    T ar = A->realp[n * IA], ai = -A->imagp[n * IA], br = B->realp[n * IB], bi = B->imagp[n * IB];
    T r = ar * br - ai * bi + C->realp[n * IC], i = ar * bi + ai * br + C->imagp[n * IC];
    D->realp[n * ID] = r; D->imagp[n * ID] = i;
  }
}
// D = A*B(Skalar) + C
void ZFN(zvsma)(const ZT *A, vDSP_Stride IA, const ZT *B, const ZT *C, vDSP_Stride IC, const ZT *D, vDSP_Stride ID, vDSP_Length N) {
  T br = B->realp[0], bi = B->imagp[0];
  LOOP {
    T ar = A->realp[n * IA], ai = A->imagp[n * IA];
    T r = ar * br - ai * bi + C->realp[n * IC], i = ar * bi + ai * br + C->imagp[n * IC];
    D->realp[n * ID] = r; D->imagp[n * ID] = i;
  }
}
// C = A * B(Skalar)
void ZFN(zvzsml)(const ZT *A, vDSP_Stride IA, const ZT *B, const ZT *C, vDSP_Stride IC, vDSP_Length N) {
  T br = B->realp[0], bi = B->imagp[0];
  LOOP {
    T ar = A->realp[n * IA], ai = A->imagp[n * IA];
    T r = ar * br - ai * bi, i = ar * bi + ai * br;
    C->realp[n * IC] = r; C->imagp[n * IC] = i;
  }
}
// komplexer Vektor mal reeller Vektor
void ZFN(zrvmul)(const ZT *A, vDSP_Stride IA, const T *B, vDSP_Stride IB, const ZT *C, vDSP_Stride IC, vDSP_Length N) {
  LOOP { T b = B[n * IB]; C->realp[n * IC] = A->realp[n * IA] * b; C->imagp[n * IC] = A->imagp[n * IA] * b; }
}
void ZFN(zvabs)(const ZT *A, vDSP_Stride IA, T *C, vDSP_Stride IC, vDSP_Length N) {
  LOOP { T r = A->realp[n * IA], i = A->imagp[n * IA]; C[n * IC] = SQRT(r * r + i * i); }
}
void ZFN(zvmags)(const ZT *A, vDSP_Stride IA, T *C, vDSP_Stride IC, vDSP_Length N) {
  LOOP { T r = A->realp[n * IA], i = A->imagp[n * IA]; C[n * IC] = r * r + i * i; }
}
void ZFN(zvphas)(const ZT *A, vDSP_Stride IA, T *C, vDSP_Stride IC, vDSP_Length N) {
  LOOP C[n * IC] = ATAN2(A->imagp[n * IA], A->realp[n * IA]);
}

// FFT. Setup: Zweierpotenz-Tabelle der Einheitswurzeln exp(-2 pi i k / 2^log2n), k < 2^(log2n-1).
// Komplexe FFT ungeordnet Radix-2 in place (Bitumkehr + Schmetterlinge), Vorzeichen -1 bei Hinrichtung.
struct CAT(FFTSetupStruct, SUF) { vDSP_Length log2n; T *wr, *wi; };

CAT(FFTSetup, SUF) CAT(vDSP_create_fftsetup, SUF)(vDSP_Length log2n, FFTRadix radix) {
  if (radix != kFFTRadix2 || log2n < 1 || log2n > 30) return NULL;   // Radix 3 und 5 fehlen
  size_t h = (size_t)1 << (log2n - 1);
  struct CAT(FFTSetupStruct, SUF) *s = malloc(sizeof *s + 2 * h * sizeof(T));
  if (!s) return NULL;
  s->log2n = log2n; s->wr = (T *)(s + 1); s->wi = s->wr + h;
  for (size_t k = 0; k < h; k++) {
    long double a = -2.0L * 3.14159265358979323846264338327950288L * (long double)k / (long double)((size_t)1 << log2n);
    s->wr[k] = (T)cosl(a); s->wi[k] = (T)sinl(a);
  }
  return (CAT(FFTSetup, SUF))s;
}
void CAT(vDSP_destroy_fftsetup, SUF)(CAT(FFTSetup, SUF) s) { free(s); }

// FFT-Kern auf zusammenhängenden Feldern der Länge 2^lg; sign = +1 Hin (exp(-i)), -1 Rück
static void CAT(fft_core, SUF)(const struct CAT(FFTSetupStruct, SUF) *s, T *re, T *im, int lg, int forward) {
  size_t n = (size_t)1 << lg;
  for (size_t i = 1, j = 0; i < n; i++) {
    size_t bit = n >> 1;
    for (; j & bit; bit >>= 1) j ^= bit;
    j ^= bit;
    if (i < j) { T t = re[i]; re[i] = re[j]; re[j] = t; t = im[i]; im[i] = im[j]; im[j] = t; }
  }
  size_t tstep = (size_t)1 << (s->log2n - 1);   // Tabellenschritt für Länge 2
  for (size_t len = 2; len <= n; len <<= 1, tstep >>= 1) {
    size_t half = len >> 1;
    for (size_t b = 0; b < n; b += len) {
      T *ar = re + b, *ai = im + b, *br = ar + half, *bi = ai + half;
      for (size_t k = 0; k < half; k++) {
        T wr = s->wr[k * tstep], wi = forward ? s->wi[k * tstep] : -s->wi[k * tstep];
        T tr = br[k] * wr - bi[k] * wi, ti = br[k] * wi + bi[k] * wr;
        br[k] = ar[k] - tr; bi[k] = ai[k] - ti; ar[k] += tr; ai[k] += ti;
      }
    }
  }
}

// Komplexe FFT mit beliebigem Stride von In nach Out (In == Out erlaubt); Temp (>= 2*2^lg) darf NULL sein
static void CAT(cfft, SUF)(const struct CAT(FFTSetupStruct, SUF) *s, const ZT *in, vDSP_Stride is, const ZT *out, vDSP_Stride os,
                           vDSP_Length lg, FFTDirection dir, T *tmp) {
  size_t n = (size_t)1 << lg;
  if (!s || lg > s->log2n || lg < 1) return;
  if (is == 1 && os == 1) {
    if (in->realp != out->realp) memcpy(out->realp, in->realp, n * sizeof(T));
    if (in->imagp != out->imagp) memcpy(out->imagp, in->imagp, n * sizeof(T));
    CAT(fft_core, SUF)(s, out->realp, out->imagp, (int)lg, dir == kFFTDirection_Forward);
    return;
  }
  T *buf = tmp ? tmp : malloc(2 * n * sizeof(T));
  for (size_t i = 0; i < n; i++) { buf[i] = in->realp[i * is]; buf[n + i] = in->imagp[i * is]; }
  CAT(fft_core, SUF)(s, buf, buf + n, (int)lg, dir == kFFTDirection_Forward);
  for (size_t i = 0; i < n; i++) { out->realp[i * os] = buf[i]; out->imagp[i * os] = buf[n + i]; }
  if (!tmp) free(buf);
}

void CAT(vDSP_fft_zip, SUF)(CAT(FFTSetup, SUF) s, const ZT *C, vDSP_Stride IC, vDSP_Length log2n, FFTDirection d) {
  CAT(cfft, SUF)((void *)s, C, IC, C, IC, log2n, d, NULL);
}
void CAT(vDSP_fft_zipt, SUF)(CAT(FFTSetup, SUF) s, const ZT *C, vDSP_Stride IC, const ZT *B, vDSP_Length log2n, FFTDirection d) {
  CAT(cfft, SUF)((void *)s, C, IC, C, IC, log2n, d, NULL);
}
void CAT(vDSP_fft_zop, SUF)(CAT(FFTSetup, SUF) s, const ZT *A, vDSP_Stride IA, const ZT *C, vDSP_Stride IC, vDSP_Length log2n, FFTDirection d) {
  CAT(cfft, SUF)((void *)s, A, IA, C, IC, log2n, d, NULL);
}
void CAT(vDSP_fft_zopt, SUF)(CAT(FFTSetup, SUF) s, const ZT *A, vDSP_Stride IA, const ZT *C, vDSP_Stride IC, const ZT *B, vDSP_Length log2n, FFTDirection d) {
  CAT(cfft, SUF)((void *)s, A, IA, C, IC, log2n, d, NULL);
}

// Reelle FFT. log2n = log2 der Zahl reeller Werte N; Daten als N/2 Split-Complex (gerade/ungerade Werte).
// Packung wie bei Apple: Index 0 = (DC, Nyquist); Ergebnis der Hinrichtung ist 2x die mathematische DFT.
static void CAT(rfft, SUF)(const struct CAT(FFTSetupStruct, SUF) *s, const ZT *in, vDSP_Stride is, const ZT *out, vDSP_Stride os,
                           vDSP_Length lg, FFTDirection dir) {
  if (!s || lg < 2) return;
  size_t h = (size_t)1 << (lg - 1);          // komplexe Länge N/2
  if (lg - 1 > s->log2n) return;
  T *buf = malloc(2 * h * sizeof(T));
  T *zr = buf, *zi = buf + h;
  for (size_t i = 0; i < h; i++) { zr[i] = in->realp[i * is]; zi[i] = in->imagp[i * is]; }
  // Tabelle für Länge N: wir haben Wurzeln für 2^s->log2n; Schritt für exp(-2 pi i k / N)
  size_t tstep = (size_t)1 << (s->log2n - lg);
  if (dir == kFFTDirection_Forward) {
    CAT(fft_core, SUF)(s, zr, zi, (int)lg - 1, 1);
    T *or_ = malloc(2 * h * sizeof(T)), *oi = or_ + h;
    or_[0] = 2 * (zr[0] + zi[0]); oi[0] = 2 * (zr[0] - zi[0]);
    for (size_t k = 1; k < h; k++) {
      T ar = zr[k], ai = zi[k], br = zr[h - k], bi = -zi[h - k];     // Z[k], conj(Z[h-k])
      T er = ar + br, ei = ai + bi;                                    // gerade Anteile * 2
      T dr = ar - br, di = ai - bi;                                    // ungerade Anteile * 2i
      // O = (d)/(2i)*2 = -i*d ; X = E + w*O  (mit Faktor 2 von Apple: X2 = E2 + w*(-i)*D)
      T wr = s->wr[k * tstep], wi = s->wi[k * tstep];
      T odr = di, odi = -dr;                                           // -i * d
      or_[k] = er + (wr * odr - wi * odi); oi[k] = ei + (wr * odi + wi * odr);
    }
    for (size_t i = 0; i < h; i++) { out->realp[i * os] = or_[i]; out->imagp[i * os] = oi[i]; }
    free(or_);
  } else {
    // Umkehrung der Hinrichtung; Ergebnis ist (wie Apple) N mal die Zeitfolge... Skalierung wird gegen Apple geprüft
    T *tr = malloc(2 * h * sizeof(T)), *ti = tr + h;
    tr[0] = zr[0] + zi[0]; ti[0] = zr[0] - zi[0];
    for (size_t k = 1; k < h; k++) {
      T ar = zr[k], ai = zi[k], br = zr[h - k], bi = -zi[h - k];
      T er = ar + br, ei = ai + bi, dr = ar - br, di = ai - bi;
      T wr = s->wr[k * tstep], wi = -s->wi[k * tstep];                 // conj(w)
      // Z[k] = E + i*conj(w)*D  (Umkehrung)
      T pr = wr * dr - wi * di, pi = wr * di + wi * dr;
      tr[k] = er - pi; ti[k] = ei + pr;
    }
    CAT(fft_core, SUF)(s, tr, ti, (int)lg - 1, 0);
    for (size_t i = 0; i < h; i++) { out->realp[i * os] = tr[i]; out->imagp[i * os] = ti[i]; }
    free(tr);
  }
  free(buf);
}
void CAT(vDSP_fft_zrip, SUF)(CAT(FFTSetup, SUF) s, const ZT *C, vDSP_Stride IC, vDSP_Length log2n, FFTDirection d) {
  CAT(rfft, SUF)((void *)s, C, IC, C, IC, log2n, d);
}
void CAT(vDSP_fft_zript, SUF)(CAT(FFTSetup, SUF) s, const ZT *C, vDSP_Stride IC, const ZT *B, vDSP_Length log2n, FFTDirection d) {
  CAT(rfft, SUF)((void *)s, C, IC, C, IC, log2n, d);
}
void CAT(vDSP_fft_zrop, SUF)(CAT(FFTSetup, SUF) s, const ZT *A, vDSP_Stride IA, const ZT *C, vDSP_Stride IC, vDSP_Length log2n, FFTDirection d) {
  CAT(rfft, SUF)((void *)s, A, IA, C, IC, log2n, d);
}
void CAT(vDSP_fft_zropt, SUF)(CAT(FFTSetup, SUF) s, const ZT *A, vDSP_Stride IA, const ZT *C, vDSP_Stride IC, const ZT *B, vDSP_Length log2n, FFTDirection d) {
  CAT(rfft, SUF)((void *)s, A, IA, C, IC, log2n, d);
}
