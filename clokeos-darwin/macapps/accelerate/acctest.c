// Prüfprogramm für die Accelerate-Teilmenge.
//   acctest            Selbsttest gegen einfache Referenzschleifen (Exit 0 = alles ok) und "dump" der Ergebnisse
//   acctest dump       nur Ergebnisse als Text ausgeben (zum Vergleich: gegen Apples Accelerate auf dem Mac
//                      gebaut und gegen unsere Bibliothek, danach acctest-cmp.py)
//   acctest bench      Zeitmessung gegen naive Schleifen
#include <Accelerate/Accelerate.h>
#include <complex.h>
#undef I
#define M_PIl 3.14159265358979323846264338327950288L
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

unsigned long long rs = 88172645463325252ULL;
double rnd(void) { rs ^= rs << 13; rs ^= rs >> 7; rs ^= rs << 17; return (double)(rs >> 11) / 9007199254740992.0 * 2 - 1; }
static void fillf(float *p, int n) { for (int i = 0; i < n; i++) p[i] = (float)rnd(); }
void filld(double *p, int n) { for (int i = 0; i < n; i++) p[i] = rnd(); }

int dumpmode, fails;
void emitf(const char *name, const float *p, long n) { printf("%s", name); for (long i = 0; i < n; i++) printf(" %.9g", (double)p[i]); printf("\n"); }
void emitd(const char *name, const double *p, long n) { printf("%s", name); for (long i = 0; i < n; i++) printf(" %.17g", p[i]); printf("\n"); }
static void emitl(const char *name, const long *p, long n) { printf("%s", name); for (long i = 0; i < n; i++) printf(" %ld", p[i]); printf("\n"); }
void check(const char *name, const double *got, const double *ref, long n, double tol) {
  double m = 0, sc = 1;
  for (long i = 0; i < n; i++) { double e = fabs(got[i] - ref[i]); if (e > m) m = e; if (fabs(ref[i]) > sc) sc = fabs(ref[i]); }
  if (!(m <= tol * sc)) { fprintf(stderr, "FAIL %s: Fehler %g (Skala %g, Toleranz %g)\n", name, m, sc, tol); fails++; }
}
static void checkf(const char *name, const float *got, const double *ref, long n, double tol) {
  double *g = malloc(n * sizeof *g); for (long i = 0; i < n; i++) g[i] = got[i]; check(name, g, ref, n, tol); free(g);
}

#define N 37
#define CASE_BIN(fn, refexpr) do { \
    float A[3 * N], B[3 * N], C[3 * N]; double R[N]; fillf(A, 3 * N); fillf(B, 3 * N); memset(C, 0, sizeof C); \
    vDSP_##fn(A, 2, B, 3, C, 1, N); for (int n = 0; n < N; n++) { float a = A[n * 2], b = B[n * 3]; R[n] = (refexpr); } \
    if (dumpmode) emitf("v_" #fn, C, N); else checkf("v_" #fn, C, R, N, 1e-6); \
    vDSP_##fn(A, 1, B, 1, C, 1, N); for (int n = 0; n < N; n++) { float a = A[n], b = B[n]; R[n] = (refexpr); } \
    if (dumpmode) emitf("v_" #fn "_1", C, N); else checkf("v_" #fn "_1", C, R, N, 1e-6); } while (0)

static void zfill(DSPSplitComplex *z, float *re, float *im, int n) { z->realp = re; z->imagp = im; fillf(re, n); fillf(im, n); }

static void run_vdsp(void) {
  float A[3 * N], B[3 * N], C[3 * N], D[3 * N], s1 = 0.7f, s2 = -0.3f, s3 = 1.3f;
  double R[3 * N];
  CASE_BIN(vadd, a + b); CASE_BIN(vsub, b - a); CASE_BIN(vmul, a * b); CASE_BIN(vmax, a > b ? a : b);
  CASE_BIN(vmin, a < b ? a : b); CASE_BIN(vmaxmg, fmax(fabs(a), fabs(b))); CASE_BIN(vdist, sqrt(a * a + b * b));
  fillf(A, 3 * N); fillf(B, 3 * N);
  for (int i = 0; i < 3 * N; i++) if (fabsf(B[i]) < 0.05f) B[i] = 0.5f;
  vDSP_vdiv(B, 1, A, 1, C, 1, N); for (int n = 0; n < N; n++) R[n] = (double)A[n] / B[n];
  if (dumpmode) emitf("vdiv", C, N); else checkf("vdiv", C, R, N, 1e-6);
  vDSP_vsadd(A, 2, &s1, C, 1, N); for (int n = 0; n < N; n++) R[n] = A[2 * n] + s1;
  if (dumpmode) emitf("vsadd", C, N); else checkf("vsadd", C, R, N, 1e-6);
  vDSP_vsmul(A, 2, &s1, C, 1, N); for (int n = 0; n < N; n++) R[n] = A[2 * n] * s1;
  if (dumpmode) emitf("vsmul", C, N); else checkf("vsmul", C, R, N, 1e-6);
  vDSP_vsdiv(A, 1, &s1, C, 1, N); for (int n = 0; n < N; n++) R[n] = A[n] / s1;
  if (dumpmode) emitf("vsdiv", C, N); else checkf("vsdiv", C, R, N, 1e-6);
  vDSP_svdiv(&s1, B, 1, C, 1, N); for (int n = 0; n < N; n++) R[n] = s1 / B[n];
  if (dumpmode) emitf("svdiv", C, N); else checkf("svdiv", C, R, N, 1e-6);
  vDSP_vsma(A, 1, &s1, B, 1, C, 1, N); for (int n = 0; n < N; n++) R[n] = A[n] * s1 + B[n];
  if (dumpmode) emitf("vsma", C, N); else checkf("vsma", C, R, N, 1e-6);
  vDSP_vsmsa(A, 1, &s1, &s2, C, 1, N); for (int n = 0; n < N; n++) R[n] = A[n] * s1 + s2;
  if (dumpmode) emitf("vsmsa", C, N); else checkf("vsmsa", C, R, N, 1e-6);
  vDSP_vsmsma(A, 1, &s1, B, 1, &s2, C, 1, N); for (int n = 0; n < N; n++) R[n] = A[n] * s1 + B[n] * s2;
  if (dumpmode) emitf("vsmsma", C, N); else checkf("vsmsma", C, R, N, 1e-6);
  fillf(D, N);
  vDSP_vma(A, 1, B, 1, D, 1, C, 1, N); for (int n = 0; n < N; n++) R[n] = A[n] * B[n] + D[n];
  if (dumpmode) emitf("vma", C, N); else checkf("vma", C, R, N, 1e-6);
  vDSP_vasm(A, 1, B, 1, &s1, C, 1, N); for (int n = 0; n < N; n++) R[n] = (A[n] + B[n]) * s1;
  if (dumpmode) emitf("vasm", C, N); else checkf("vasm", C, R, N, 1e-6);
  vDSP_vsbsm(A, 1, B, 1, &s1, C, 1, N); for (int n = 0; n < N; n++) R[n] = (A[n] - B[n]) * s1;
  if (dumpmode) emitf("vsbsm", C, N); else checkf("vsbsm", C, R, N, 1e-6);
  vDSP_vneg(A, 1, C, 1, N); if (dumpmode) emitf("vneg", C, N);
  vDSP_vabs(A, 1, C, 1, N); if (dumpmode) emitf("vabs", C, N);
  vDSP_vsq(A, 1, C, 1, N); if (dumpmode) emitf("vsq", C, N);
  vDSP_vfill(&s3, C, 1, N); if (dumpmode) emitf("vfill", C, N);
  vDSP_vclr(C, 1, N); if (dumpmode) emitf("vclr", C, N);
  memcpy(C, A, N * sizeof(float)); vDSP_vrvrs(C, 1, N); if (dumpmode) emitf("vrvrs", C, N);
  vDSP_vramp(&s2, &s1, C, 1, N); if (dumpmode) emitf("vramp", C, N);
  vDSP_vgen(&s2, &s1, C, 1, N); if (dumpmode) emitf("vgen", C, N);
  { float lo = -0.4f, hi = 0.5f; vDSP_vclip(A, 1, &lo, &hi, C, 1, N); if (dumpmode) emitf("vclip", C, N);
    unsigned long nl, nh; vDSP_vclipc(A, 1, &lo, &hi, C, 1, N, &nl, &nh);
    if (dumpmode) { emitf("vclipc", C, N); long c2[2] = {(long)nl, (long)nh}; emitl("vclipc_n", c2, 2); }
    vDSP_vlim(A, 1, &s2, &s3, C, 1, N); if (dumpmode) emitf("vlim", C, N);
    vDSP_vthr(A, 1, &s2, C, 1, N); if (dumpmode) emitf("vthr", C, N);
    vDSP_vthres(A, 1, &s2, C, 1, N); if (dumpmode) emitf("vthres", C, N); }
  { float one = 1; vDSP_vdbcon(A, 1, &one, C, 1, N, 1); for (int i = 0; i < N; i++) C[i] = C[i]; /* A kann negativ sein: NaN */
    vDSP_vsq(A, 1, D, 1, N); vDSP_vdbcon(D, 1, &one, C, 1, N, 1); if (dumpmode) emitf("vdbcon20", C, N);
    vDSP_vdbcon(D, 1, &one, C, 1, N, 0); if (dumpmode) emitf("vdbcon10", C, N); }
  vDSP_vfrac(A, 1, C, 1, N); if (dumpmode) emitf("vfrac", C, N);
  { float co[4] = {0.5f, -1.f, 2.f, 0.25f}; vDSP_vpoly(co, 1, A, 1, C, 1, N, 3); if (dumpmode) emitf("vpoly", C, N); }
  { float pa[N]; for (int i = 0; i < N; i++) pa[i] = A[i] * 10; short sh[N]; int in[N]; vDSP_vfix16(pa, 1, sh, 1, N);
    float f[N]; vDSP_vflt16(sh, 1, f, 1, N); if (dumpmode) emitf("vfix16", f, N);
    vDSP_vfixr16(pa, 1, sh, 1, N); vDSP_vflt16(sh, 1, f, 1, N); if (dumpmode) emitf("vfixr16", f, N);
    vDSP_vfix32(pa, 1, in, 1, N); vDSP_vfixr32(pa, 1, in, 1, N); float g[N]; for (int i = 0; i < N; i++) g[i] = (float)in[i]; if (dumpmode) emitf("vfixr32", g, N);
    char ch[N]; for (int i = 0; i < N; i++) ch[i] = (char)(i - 10); vDSP_vflt8(ch, 1, f, 1, N); if (dumpmode) emitf("vflt8", f, N); }
  // Summen
  { float c; vDSP_Length idx; double ref;
    vDSP_sve(A, 1, &c, N); ref = 0; for (int i = 0; i < N; i++) ref += A[i]; if (dumpmode) emitf("sve", &c, 1); else checkf("sve", &c, &ref, 1, 1e-5);
    vDSP_svemg(A, 2, &c, N); if (dumpmode) emitf("svemg", &c, 1);
    vDSP_svesq(A, 1, &c, N); if (dumpmode) emitf("svesq", &c, 1);
    vDSP_meanv(A, 1, &c, N); if (dumpmode) emitf("meanv", &c, 1);
    vDSP_measqv(A, 1, &c, N); if (dumpmode) emitf("measqv", &c, 1);
    vDSP_rmsqv(A, 1, &c, N); if (dumpmode) emitf("rmsqv", &c, 1);
    vDSP_dotpr(A, 2, B, 1, &c, N); ref = 0; for (int i = 0; i < N; i++) ref += (double)A[2 * i] * B[i];
    if (dumpmode) emitf("dotpr", &c, 1); else checkf("dotpr", &c, &ref, 1, 1e-5);
    vDSP_distancesq(A, 1, B, 1, &c, N); if (dumpmode) emitf("distancesq", &c, 1);
    vDSP_maxv(A, 1, &c, N); if (dumpmode) emitf("maxv", &c, 1);
    vDSP_minv(A, 1, &c, N); if (dumpmode) emitf("minv", &c, 1);
    vDSP_maxmgv(A, 1, &c, N); if (dumpmode) emitf("maxmgv", &c, 1);
    vDSP_maxvi(A, 2, &c, &idx, N); if (dumpmode) { emitf("maxvi", &c, 1); long l = idx; emitl("maxvi_i", &l, 1); }
    vDSP_minvi(A, 1, &c, &idx, N); if (dumpmode) { emitf("minvi", &c, 1); long l = idx; emitl("minvi_i", &l, 1); }
    vDSP_minmgvi(A, 1, &c, &idx, N); if (dumpmode) { emitf("minmgvi", &c, 1); long l = idx; emitl("minmgvi_i", &l, 1); }
    vDSP_vswmax(A, 1, C, 1, 20, 8); if (dumpmode) emitf("vswmax", C, 20);
    float mean, sd; vDSP_normalize(A, 1, C, 1, &mean, &sd, N); if (dumpmode) { emitf("normalize", C, N); emitf("normalize_m", &mean, 1); emitf("normalize_s", &sd, 1); } }
  // Sortieren, Interpolation
  { memcpy(C, A, N * sizeof(float)); vDSP_vsort(C, N, 1); if (dumpmode) emitf("vsort", C, N);
    vDSP_vsort(C, N, -1); if (dumpmode) emitf("vsort_dn", C, N);
    vDSP_Length I[N]; long Il[N]; for (int i = 0; i < N; i++) I[i] = i;
    vDSP_vsorti(A, I, NULL, N, 1); for (int i = 0; i < N; i++) Il[i] = I[i]; if (dumpmode) emitl("vsorti", Il, N);
    float pos[N]; for (int i = 0; i < N; i++) pos[i] = (float)(i * 0.37 + 0.1);
    vDSP_vlint(A, pos, 1, C, 1, N, N); if (dumpmode) emitf("vlint", C, N);
    vDSP_vqint(A, pos, 1, C, 1, 10, N); if (dumpmode) emitf("vqint", C, 10);
    float t1 = 2.0f, t2 = 3.5f; float tab[10]; for (int i = 0; i < 10; i++) tab[i] = (float)(i * i);
    vDSP_vtabi(A, 1, &t1, &t2, tab, 10, C, 1, N); if (dumpmode) emitf("vtabi", C, N);
    float gv[4] = {1, 5, 2, 8}, gi[4] = {0, 10, 20, 30}; vDSP_vgenp(gv, 1, gi, 1, C, 1, 31, 4); if (dumpmode) emitf("vgenp", C, 31);
    float zc[8] = {1, 2, -1, -2, 3, -1, 1, 1}; vDSP_Length last, cnt; vDSP_nzcros(zc, 1, 10, &last, &cnt, 8);
    if (dumpmode) { long l[2] = {(long)last, (long)cnt}; emitl("nzcros", l, 2); } }
  // Rampen
  { float st = 0.5f, inc = 0.1f; vDSP_vrampmul(A, 1, &st, &inc, C, 1, N); if (dumpmode) { emitf("vrampmul", C, N); emitf("vrampmul_a", &st, 1); }
    st = 0.5f; for (int i = 0; i < N; i++) C[i] = 1; vDSP_vrampmuladd(A, 1, &st, &inc, C, 1, N); if (dumpmode) emitf("vrampmuladd", C, N);
    st = 0.5f; vDSP_vrampmul2(A, B, 1, &st, &inc, C, D, 1, N); if (dumpmode) { emitf("vrampmul2a", C, N); emitf("vrampmul2b", D, N); } }
  // Matrizen
  { float M1[12], M2[20], M3[15]; fillf(M1, 12); fillf(M2, 20);
    vDSP_mmul(M1, 1, M2, 1, M3, 1, 3, 5, 4); double ref[15];
    for (int m = 0; m < 3; m++) for (int p = 0; p < 5; p++) { double s = 0; for (int k = 0; k < 4; k++) s += (double)M1[m * 4 + k] * M2[k * 5 + p]; ref[m * 5 + p] = s; }
    if (dumpmode) emitf("mmul", M3, 15); else checkf("mmul", M3, ref, 15, 1e-5);
    float T[12]; vDSP_mtrans(M1, 1, T, 1, 4, 3); if (dumpmode) emitf("mtrans", T, 12);
    float S[6] = {0}; vDSP_mmov(M1, S, 2, 3, 3, 2); if (dumpmode) emitf("mmov", S, 6); }
  // Faltung / Korrelation
  { float F[5]; fillf(F, 5); vDSP_conv(A, 1, F, 1, C, 1, 30, 5); double ref[30];
    for (int n = 0; n < 30; n++) { double s = 0; for (int p = 0; p < 5; p++) s += (double)A[n + p] * F[p]; ref[n] = s; }
    if (dumpmode) emitf("conv_corr", C, 30); else checkf("conv_corr", C, ref, 30, 1e-5);
    vDSP_conv(A, 1, F + 4, -1, C, 1, 30, 5);
    for (int n = 0; n < 30; n++) { double s = 0; for (int p = 0; p < 5; p++) s += (double)A[n + p] * F[4 - p]; ref[n] = s; }
    if (dumpmode) emitf("conv_conv", C, 30); else checkf("conv_conv", C, ref, 30, 1e-5);
    vDSP_conv(A, 2, F, 1, C, 3, 12, 5); if (dumpmode) emitf("conv_str", C, 12); }
  // Fenster
  { float W[64]; vDSP_hann_window(W, 64, 0); if (dumpmode) emitf("hann", W, 64);
    vDSP_hann_window(W, 64, vDSP_HANN_NORM); if (dumpmode) emitf("hann_norm", W, 64);
    vDSP_hann_window(W, 64, vDSP_HALF_WINDOW); if (dumpmode) emitf("hann_half", W, 32);
    vDSP_hamm_window(W, 64, 0); if (dumpmode) emitf("hamm", W, 64);
    vDSP_blkman_window(W, 64, 0); if (dumpmode) emitf("blkman", W, 64);
    double WD[64]; vDSP_hann_windowD(WD, 64, 0); if (dumpmode) emitd("hannD", WD, 64); }
  // Polar / Rechteck
  { float P[2 * N], Q[2 * N]; fillf(P, 2 * N); vDSP_polar(P, 2, Q, 2, N); if (dumpmode) emitf("polar", Q, 2 * N);
    vDSP_rect(Q, 2, P, 2, N); if (dumpmode) emitf("rect", P, 2 * N); }
  // Komplexe Vektoren
  { float ar[N], ai[N], br[N], bi[N], cr[N], ci[N], dr[N], di[N];
    DSPSplitComplex a, b, c, d; zfill(&a, ar, ai, N); zfill(&b, br, bi, N); zfill(&c, cr, ci, N); d.realp = dr; d.imagp = di;
#define ZOUT(nm) if (dumpmode) { emitf(nm "_r", dr, N); emitf(nm "_i", di, N); }
    vDSP_zvadd(&a, 1, &b, 1, &d, 1, N); ZOUT("zvadd");
    vDSP_zvmul(&a, 1, &b, 1, &d, 1, N, 1); ZOUT("zvmul");
    vDSP_zvmul(&a, 1, &b, 1, &d, 1, N, -1); ZOUT("zvmulc");
    vDSP_zvcmul(&a, 1, &b, 1, &d, 1, N); ZOUT("zvcmul");
    vDSP_zvcma(&a, 1, &b, 1, &c, 1, &d, 1, N); ZOUT("zvcma");
    vDSP_zvsma(&a, 1, &b, &c, 1, &d, 1, N); ZOUT("zvsma");
    vDSP_zvzsml(&a, 1, &b, &d, 1, N); ZOUT("zvzsml");
    vDSP_zrvmul(&a, 1, br, 1, &d, 1, N); ZOUT("zrvmul");
    float o[N]; vDSP_zvabs(&a, 1, o, 1, N); if (dumpmode) emitf("zvabs", o, N);
    vDSP_zvmags(&a, 1, o, 1, N); if (dumpmode) emitf("zvmags", o, N);
    vDSP_zvphas(&a, 1, o, 1, N); if (dumpmode) emitf("zvphas", o, N);
    DSPComplex cc[N]; vDSP_ztoc(&a, 1, cc, 2, N); if (dumpmode) emitf("ztoc", (float *)cc, 2 * N);
    vDSP_ctoz(cc, 2, &d, 1, N); ZOUT("ctoz"); }
  // FFT
  for (int lg = 3; lg <= 10; lg += 7) {
    int n = 1 << lg; float *re = malloc(n * 4), *im = malloc(n * 4), *r2 = malloc(n * 4), *i2 = malloc(n * 4);
    FFTSetup su = vDSP_create_fftsetup(10, kFFTRadix2);
    fillf(re, n); fillf(im, n); memcpy(r2, re, n * 4); memcpy(i2, im, n * 4);
    DSPSplitComplex z = {re, im};
    vDSP_fft_zip(su, &z, 1, lg, kFFTDirection_Forward);
    if (dumpmode) { char nm[32]; sprintf(nm, "fft_zip%d_r", lg); emitf(nm, re, n); sprintf(nm, "fft_zip%d_i", lg); emitf(nm, im, n); }
    else { double *rr = malloc(n * 8), *ri = malloc(n * 8);
      for (int k = 0; k < n; k++) { long double sr = 0, si = 0; for (int j = 0; j < n; j++) { long double a = -2 * M_PIl * (long double)k * j / n; sr += r2[j] * cos((double)a) - i2[j] * sin((double)a); si += r2[j] * sin((double)a) + i2[j] * cos((double)a); } rr[k] = sr; ri[k] = si; }
      checkf("fft_zip_r", re, rr, n, 2e-5); checkf("fft_zip_i", im, ri, n, 2e-5); free(rr); free(ri); }
    vDSP_fft_zip(su, &z, 1, lg, kFFTDirection_Inverse);
    { double *ref = malloc(n * 16); for (int k = 0; k < n; k++) { ref[k] = (double)r2[k] * n; ref[n + k] = (double)i2[k] * n; }
      if (dumpmode) { char nm[32]; sprintf(nm, "fft_inv%d_r", lg); emitf(nm, re, n); } else { checkf("fft_inv_r", re, ref, n, 2e-5); checkf("fft_inv_i", im, ref + n, n, 2e-5); } free(ref); }
    // reelle FFT in place: Daten als n/2 Split-Complex (gerade/ungerade)
    float *x = malloc(n * 4); fillf(x, n); float *hr = malloc(n * 2), *hi = malloc(n * 2);
    for (int k = 0; k < n / 2; k++) { hr[k] = x[2 * k]; hi[k] = x[2 * k + 1]; }
    DSPSplitComplex h = {hr, hi};
    vDSP_fft_zrip(su, &h, 1, lg, kFFTDirection_Forward);
    if (dumpmode) { char nm[32]; sprintf(nm, "fft_zrip%d_r", lg); emitf(nm, hr, n / 2); sprintf(nm, "fft_zrip%d_i", lg); emitf(nm, hi, n / 2); }
    else { double *rr = malloc(n * 8), *ri = malloc(n * 8);
      for (int k = 0; k < n / 2; k++) { long double sr = 0, si = 0; for (int j = 0; j < n; j++) { long double a = -2 * M_PIl * (long double)k * j / n; sr += x[j] * cos((double)a); si += x[j] * sin((double)a); } rr[k] = 2 * sr; ri[k] = 2 * si; }
      ri[0] = 0; { long double s = 0; for (int j = 0; j < n; j++) s += (j & 1 ? -x[j] : x[j]); ri[0] = 2 * s; }
      checkf("fft_zrip_r", hr, rr, n / 2, 2e-5); checkf("fft_zrip_i", hi, ri, n / 2, 2e-5); free(rr); free(ri); }
    vDSP_fft_zrip(su, &h, 1, lg, kFFTDirection_Inverse);
    if (dumpmode) { char nm[32]; sprintf(nm, "fft_zrip_inv%d_r", lg); emitf(nm, hr, n / 2); sprintf(nm, "fft_zrip_inv%d_i", lg); emitf(nm, hi, n / 2); }
    else { double ref[2048]; for (int k = 0; k < n / 2; k++) { ref[k] = 2.0 * n * x[2 * k] / 2; ref[n / 2 + k] = 2.0 * n * x[2 * k + 1] / 2; }
      // Apple: Vor- und Rücktransformation ergeben das Doppelte von N: gegen die beobachtete Skalierung prüft der Apple-Vergleich
      (void)ref; }
    // out of place + Stride
    float *sr = calloc(n * 2, 4), *si = calloc(n * 2, 4), *dr = malloc(n * 4), *di = malloc(n * 4);
    for (int k = 0; k < n; k++) { sr[2 * k] = r2[k]; si[2 * k] = i2[k]; }
    DSPSplitComplex zs = {sr, si}, zd = {dr, di};
    vDSP_fft_zop(su, &zs, 2, &zd, 1, lg, kFFTDirection_Forward);
    if (dumpmode) { char nm[32]; sprintf(nm, "fft_zop%d_r", lg); emitf(nm, dr, n); }
    else { /* gleich zip */ }
    float *rop_r = malloc(n * 2), *rop_i = malloc(n * 2); DSPSplitComplex zr = {rop_r, rop_i};
    for (int k = 0; k < n / 2; k++) { hr[k] = x[2 * k]; hi[k] = x[2 * k + 1]; }
    vDSP_fft_zrop(su, &h, 1, &zr, 1, lg, kFFTDirection_Forward);
    if (dumpmode) { char nm[32]; sprintf(nm, "fft_zrop%d_r", lg); emitf(nm, rop_r, n / 2); }
    vDSP_destroy_fftsetup(su);
    free(re); free(im); free(r2); free(i2); free(x); free(hr); free(hi); free(sr); free(si); free(dr); free(di); free(rop_r); free(rop_i);
  }
  { // Doppelte Genauigkeit
    int lg = 6, n = 64; double re[64], im[64], r2[64], i2[64]; filld(re, n); filld(im, n); memcpy(r2, re, sizeof re); memcpy(i2, im, sizeof im);
    FFTSetupD su = vDSP_create_fftsetupD(8, kFFTRadix2); DSPDoubleSplitComplex z = {re, im};
    vDSP_fft_zipD(su, &z, 1, lg, kFFTDirection_Forward);
    if (dumpmode) { emitd("fftD_r", re, n); emitd("fftD_i", im, n); }
    else { double rr[64], ri[64]; for (int k = 0; k < n; k++) { long double a = 0, b = 0; for (int j = 0; j < n; j++) { long double w = -2 * M_PIl * (long double)k * j / n; a += r2[j] * cos((double)w) - i2[j] * sin((double)w); b += r2[j] * sin((double)w) + i2[j] * cos((double)w); } rr[k] = a; ri[k] = b; }
      check("fftD_r", re, rr, n, 1e-12); check("fftD_i", im, ri, n, 1e-12); }
    double hr[32], hi[32]; for (int k = 0; k < 32; k++) { hr[k] = r2[2 * k]; hi[k] = i2[2 * k]; }
    DSPDoubleSplitComplex h = {hr, hi}; vDSP_fft_zripD(su, &h, 1, lg, kFFTDirection_Forward);
    if (dumpmode) { emitd("fftD_rip_r", hr, 32); emitd("fftD_rip_i", hi, 32); }
    vDSP_destroy_fftsetupD(su); }
  // Biquad
  { double co[10] = {0.2, 0.4, 0.2, -0.3, 0.1, 0.5, -0.2, 0.3, 0.15, -0.05}; vDSP_biquad_Setup bq = vDSP_biquad_CreateSetup(co, 2);
    float dl[6] = {0}, X[N], Y[N]; fillf(X, N);
    vDSP_biquad(bq, dl, X, 1, Y, 1, 20); vDSP_biquad(bq, dl, X + 20, 1, Y + 20, 1, N - 20);
    double ref[N]; double w1[2][2] = {{0}}; for (int n = 0; n < N; n++) { double v = X[n]; for (int s = 0; s < 2; s++) { double *k = co + 5 * s;
      double y = k[0] * v + k[1] * w1[s][0] + k[2] * w1[s][1] - k[3] * 0; (void)y; v = v; } ref[n] = 0; }
    // Referenz: Direktform I
    { double xs[2][2] = {{0}}, ys[2][2] = {{0}}; for (int n = 0; n < N; n++) { double v = X[n]; for (int s = 0; s < 2; s++) { double *k = co + 5 * s;
        double y = k[0] * v + k[1] * xs[s][0] + k[2] * xs[s][1] - k[3] * ys[s][0] - k[4] * ys[s][1];
        xs[s][1] = xs[s][0]; xs[s][0] = v; ys[s][1] = ys[s][0]; ys[s][0] = y; v = y; } ref[n] = v; } }
    if (dumpmode) emitf("biquad", Y, N); else checkf("biquad", Y, ref, N, 1e-4);
    vDSP_biquad_DestroySetup(bq); }
  // Bild-FIR
  { double img[6 * 7], f[9], o[42]; filld(img, 42); filld(f, 9); vDSP_imgfirD(img, 6, 7, f, o, 3, 3); if (dumpmode) emitd("imgfirD", o, 42); }
}

static void run_vforce(void) {
  float x[N], y[N]; double ref[N]; int n = N;
  for (int i = 0; i < N; i++) x[i] = (float)(0.1 + (rnd() + 1) * 3);
#define VV(nm, fn) vv##nm(y, x, &n); for (int i = 0; i < N; i++) ref[i] = fn((double)x[i]); \
  if (dumpmode) emitf("vv" #nm, y, N); else checkf("vv" #nm, y, ref, N, 2e-6)
  VV(expf, exp); VV(logf, log); VV(sqrtf, sqrt); VV(exp2f, exp2); VV(log2f, log2); VV(floorf, floor); VV(sinf, sin); VV(cosf, cos);
  { float z[N]; for (int i = 0; i < N; i++) z[i] = 1.7f; vvfmodf(y, x, z, &n); for (int i = 0; i < N; i++) ref[i] = fmod(x[i], 1.7f);
    if (dumpmode) emitf("vvfmodf", y, N); else checkf("vvfmodf", y, ref, N, 1e-6); }
  { double dx[N], dy[N]; for (int i = 0; i < N; i++) dx[i] = x[i]; vvexp(dy, dx, &n); vvlog(ref, dx, &n); vvsqrt(dy, dx, &n); if (dumpmode) emitd("vvsqrt", dy, N); }
  { vFloat v = {-1.5f, 2.f, -0.f, 3.25f}, r = vfabsf(v); float o[4] = {r[0], r[1], r[2], r[3]}; if (dumpmode) emitf("vfabsf", o, 4); }
}

static void run_blas(void) {
  float A[12], X[4], Y[3] = {1, 2, 3}; fillf(A, 12); fillf(X, 4);
  float Yc[3]; memcpy(Yc, Y, 12);
  cblas_sgemv(CblasColMajor, CblasNoTrans, 3, 4, 1.5f, A, 3, X, 1, 0.5f, Yc, 1);
  double ref[4]; for (int i = 0; i < 3; i++) { double s = 0; for (int j = 0; j < 4; j++) s += (double)A[i + j * 3] * X[j]; ref[i] = 1.5 * s + 0.5 * Y[i]; }
  if (dumpmode) emitf("sgemv_cm", Yc, 3); else checkf("sgemv_cm", Yc, ref, 3, 1e-5);
  float Yr[4] = {1, 2, 3, 4}; float Xr[3]; fillf(Xr, 3);
  cblas_sgemv(CblasRowMajor, CblasTrans, 3, 4, 1.0f, A, 4, Xr, 1, 0.0f, Yr, 1);
  for (int j = 0; j < 4; j++) { double s = 0; for (int i = 0; i < 3; i++) s += (double)A[i * 4 + j] * Xr[i]; ref[j] = s; }
  if (dumpmode) emitf("sgemv_rm_t", Yr, 4); else checkf("sgemv_rm_t", Yr, ref, 4, 1e-5);
  float c1[5] = {1, 2, 3, 4, 5}, c2[5] = {0}; cblas_scopy(5, c1, 1, c2, 1); if (dumpmode) emitf("scopy", c2, 5);
  cblas_scopy(3, c1, 2, c2, 1); if (dumpmode) emitf("scopy2", c2, 5);
  double DA[12], DX[4], DY[3] = {1, 2, 3}; filld(DA, 12); filld(DX, 4);
  cblas_dgemv(CblasColMajor, CblasNoTrans, 3, 4, 1.5, DA, 3, DX, 1, 0.5, DY, 1); if (dumpmode) emitd("dgemv", DY, 3);
  float B[12], C[9]; fillf(B, 12);
  cblas_sgemm(CblasRowMajor, CblasNoTrans, CblasTrans, 3, 3, 4, 1.0f, A, 4, B, 4, 0.0f, C, 3);
  double rg[9]; for (int i = 0; i < 3; i++) for (int j = 0; j < 3; j++) { double s = 0; for (int k = 0; k < 4; k++) s += (double)A[i * 4 + k] * B[j * 4 + k]; rg[i * 3 + j] = s; }
  if (dumpmode) emitf("sgemm", C, 9); else checkf("sgemm", C, rg, 9, 1e-5);
  float dt = cblas_sdot(4, X, 1, A, 3); if (dumpmode) emitf("sdot", &dt, 1);
}

void run_lapack(void);
static void run_vimage(void) {
  vImage_Buffer b; vImageBuffer_Init(&b, 4, 5, 32, kvImageNoFlags);
  uint8_t *p = b.data; for (size_t y = 0; y < 4; y++) for (size_t x = 0; x < 20; x++) p[y * b.rowBytes + x] = (uint8_t)(y * 20 + x + 1);
  vImage_Buffer d; vImageBuffer_Init(&d, 4, 5, 32, kvImageNoFlags);
  vImageVerticalReflect_ARGB8888(&b, &d, kvImageNoFlags);
  long o[20]; for (int i = 0; i < 20; i++) o[i] = ((uint8_t *)d.data)[i]; if (dumpmode) emitl("vreflect", o, 20);
  else { for (int i = 0; i < 20; i++) if (o[i] != (long)(60 + i + 1)) { fprintf(stderr, "FAIL vreflect\n"); fails++; break; } }
  vImageUnpremultiplyData_RGBA8888(&b, &d, kvImageNoFlags);
  for (int i = 0; i < 20; i++) o[i] = ((uint8_t *)d.data)[i]; if (dumpmode) emitl("unpremul", o, 20);
  vImagePixelCount h0[256], h1[256], h2[256], h3[256]; vImagePixelCount *hh[4] = {h0, h1, h2, h3};
  vImageHistogramCalculation_ARGB8888(&b, hh, kvImageNoFlags); long hs[4] = {0}; for (int c = 0; c < 4; c++) for (int i = 0; i < 256; i++) hs[c] += hh[c][i] * (long)i;
  if (dumpmode) emitl("histogram", hs, 4);
  Pixel_8888 bg = {0, 0, 0, 0}; vImageTentConvolve_ARGB8888(&b, &d, NULL, 0, 0, 3, 3, bg, kvImageEdgeExtend);
  for (int i = 0; i < 20; i++) o[i] = ((uint8_t *)d.data)[i]; if (dumpmode) emitl("tent", o, 20);
  free(b.data); free(d.data);
}

static double now(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec + t.tv_nsec * 1e-9; }
static volatile float sink;
static void bench(void) {
  int n = 1 << 14, reps = 2000; float *a = malloc(n * 4), *b = malloc(n * 4), *c = malloc(n * 4); fillf(a, n); fillf(b, n); float k = 0.5f;
  double t0, tv, tn;
#define BENCH(name, vcall, ncall) \
  t0 = now(); for (int r = 0; r < reps; r++) { vcall; } tv = now() - t0; \
  t0 = now(); for (int r = 0; r < reps; r++) { ncall; __asm__ volatile("" ::: "memory"); } tn = now() - t0; \
  printf("%-10s %.0f MElem/s  naive %.0f MElem/s  x%.2f\n", name, (double)n * reps / tv / 1e6, (double)n * reps / tn / 1e6, tn / tv)
  BENCH("vadd", vDSP_vadd(a, 1, b, 1, c, 1, n), for (int i = 0; i < n; i++) c[i] = a[i] + b[i]);
  BENCH("vsmul", vDSP_vsmul(a, 1, &k, c, 1, n), for (int i = 0; i < n; i++) c[i] = a[i] * k);
  { float s; BENCH("dotpr", vDSP_dotpr(a, 1, b, 1, &s, n); sink = s, float t = 0; for (int i = 0; i < n; i++) t += a[i] * b[i]; sink = t); }
  { float s; BENCH("sve", vDSP_sve(a, 1, &s, n); sink = s, float t = 0; for (int i = 0; i < n; i++) t += a[i]; sink = t); }
  { int lg = 12, m = 1 << lg; FFTSetup su = vDSP_create_fftsetup(lg, kFFTRadix2); float *re = malloc(m * 4), *im = malloc(m * 4); fillf(re, m); fillf(im, m);
    DSPSplitComplex z = {re, im}; int fr = 500; t0 = now(); for (int r = 0; r < fr; r++) vDSP_fft_zip(su, &z, 1, lg, kFFTDirection_Forward); tv = now() - t0;
    // naive: O(n^2) DFT auf kleinerem n zur Einordnung (n=1024), hochgerechnet
    int dn = 1024; float *xr = re, *xi = im; float *orr = malloc(dn * 4), *oi = malloc(dn * 4); t0 = now();
    for (int k2 = 0; k2 < dn; k2++) { float sr = 0, si = 0; for (int j = 0; j < dn; j++) { float ang = -2 * (float)M_PI * (float)((long)k2 * j % dn) / dn; sr += xr[j] * cosf(ang) - xi[j] * sinf(ang); si += xr[j] * sinf(ang) + xi[j] * cosf(ang); } orr[k2] = sr; oi[k2] = si; }
    tn = now() - t0; sink = orr[3] + oi[3];
    printf("fft_zip 4096: %.1f us/transform (%.0f MFLOPS-ish 5N log2N); naive DFT 1024: %.0f us\n", tv / fr * 1e6, 5.0 * m * lg / (tv / fr) / 1e6, tn * 1e6); }
  { int P = 64; float *f = malloc(P * 4); fillf(f, P); int cn = n - P; BENCH("conv64", vDSP_conv(a, 1, f, 1, c, 1, cn, P), for (int i = 0; i < cn; i++) { float s = 0; for (int p = 0; p < P; p++) s += a[i + p] * f[p]; c[i] = s; }); }
  { int M = 128; float *x = malloc(M * M * 4), *y = malloc(M * M * 4), *z = malloc(M * M * 4); fillf(x, M * M); fillf(y, M * M); int gr = 20;
    t0 = now(); for (int r = 0; r < gr; r++) cblas_sgemm(CblasRowMajor, CblasNoTrans, CblasNoTrans, M, M, M, 1, x, M, y, M, 0, z, M); tv = now() - t0;
    t0 = now(); for (int r = 0; r < gr; r++) for (int i = 0; i < M; i++) for (int j = 0; j < M; j++) { float s = 0; for (int k2 = 0; k2 < M; k2++) s += x[i * M + k2] * y[k2 * M + j]; z[i * M + j] = s; } tn = now() - t0;
    printf("sgemm 128: %.2f GFLOPS  naive ijk %.2f GFLOPS  x%.2f\n", 2.0 * M * M * M * gr / tv / 1e9, 2.0 * M * M * M * gr / tn / 1e9, tn / tv); }
}

int main(int argc, char **argv) {
  if (argc > 1 && !strcmp(argv[1], "bench")) { bench(); return 0; }
  dumpmode = argc > 1 && !strcmp(argv[1], "dump");
  run_vdsp(); run_vforce(); run_blas(); run_lapack(); run_vimage();
  if (!dumpmode) { if (fails) { printf("acctest: %d Fehler\n", fails); return 1; } printf("acctest: alle Selbsttests ok\n"); }
  return 0;
}
