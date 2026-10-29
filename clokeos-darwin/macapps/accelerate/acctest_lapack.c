// LAPACK-Teil des Prüfprogramms (braucht die ILP64-Header, daher eigene Übersetzungseinheit)
#define ACCELERATE_NEW_LAPACK
#define ACCELERATE_LAPACK_ILP64
#include <Accelerate/Accelerate.h>
#include <complex.h>
#undef I
#include <math.h>
#include <stdio.h>
#include <string.h>
#define Ci _Complex_I
double acc_cabs(double _Complex);
#define cabs acc_cabs   // PureDarwins libSystem hat kein cabs (ccompat.c)
extern int dumpmode, fails;
double rnd(void); void filld(double *, int);
void emitd(const char *, const double *, long);
void check(const char *, const double *, const double *, long, double);
// LAPACK: Prüfung über Residuen, vergleichbar zwischen Implementierungen
void run_lapack(void) {
  __LAPACK_int n = 6, nrhs = 2, lda = 6, ipiv[6], info;
  double A0[36], A[36], B0[12], B[12]; filld(A0, 36); filld(B0, 12);
  for (int i = 0; i < 6; i++) A0[i + i * 6] += 3;
  memcpy(A, A0, sizeof A); memcpy(B, B0, sizeof B);
  dgesv_(&n, &nrhs, A, &lda, ipiv, B, &n, &info);
  double res[12]; for (int c = 0; c < 2; c++) for (int i = 0; i < 6; i++) { double s = -B0[i + c * 6]; for (int j = 0; j < 6; j++) s += A0[i + j * 6] * B[j + c * 6]; res[i + c * 6] = s; }
  double zero[12] = {0}; if (info) { fprintf(stderr, "FAIL dgesv info %ld\n", info); fails++; }
  if (dumpmode) emitd("dgesv", B, 12); else check("dgesv_resid", res, zero, 12, 1e-12);
  memcpy(A, A0, sizeof A); dgetrf_(&n, &n, A, &lda, ipiv, &info);
  double w[64]; __LAPACK_int lw = 64; dgetri_(&n, A, &lda, ipiv, w, &lw, &info);
  double id[36], ei[36]; for (int i = 0; i < 6; i++) for (int j = 0; j < 6; j++) { double s = 0; for (int k = 0; k < 6; k++) s += A0[i + k * 6] * A[k + j * 6]; id[i + j * 6] = s; ei[i + j * 6] = i == j; }
  if (dumpmode) emitd("dgetri", A, 36); else check("dgetri_resid", id, ei, 36, 1e-12);
  { // symmetrisch
    double S[36], S0[36]; for (int i = 0; i < 6; i++) for (int j = 0; j <= i; j++) { S0[i + j * 6] = S0[j + i * 6] = A0[i + j * 6]; }
    memcpy(S, S0, sizeof S); memcpy(B, B0, sizeof B); char up = 'U'; dsysv_(&up, &n, &nrhs, S, &lda, ipiv, B, &n, w, &lw, &info);
    if (dumpmode) emitd("dsysv", B, 12); }
  { // komplex zcgesv
    double _Complex ZA[16], ZB[8], ZX[8], ZA0[16]; __LAPACK_int zn = 4, zr = 2, zl = 4, zi[4], iter; double _Complex zw[8]; __LAPACK_float_complex sw[48]; double rw[4];
    for (int i = 0; i < 16; i++) ZA0[i] = rnd() + Ci * rnd(); for (int i = 0; i < 4; i++) ZA0[i + i * 4] += 3; for (int i = 0; i < 8; i++) ZB[i] = rnd() + Ci * rnd();
    memcpy(ZA, ZA0, sizeof ZA);
    zcgesv_(&zn, &zr, (__LAPACK_double_complex *)ZA, &zl, zi, (__LAPACK_double_complex *)ZB, &zl, (__LAPACK_double_complex *)ZX, &zl, (__LAPACK_double_complex *)zw, sw, rw, &iter, &info);
    double r[8], z8[8] = {0}; for (int c = 0; c < 2; c++) for (int i = 0; i < 4; i++) { double _Complex s = -ZB[i + c * 4]; for (int j = 0; j < 4; j++) s += ZA0[i + j * 4] * ZX[j + c * 4]; r[i + c * 4] = cabs(s); }
    check("zcgesv_resid", r, z8, 8, 1e-12); if (info) { fprintf(stderr, "FAIL zcgesv info\n"); fails++; } }
  for (int shape = 0; shape < 3; shape++) {   // SVD: 6x4, 4x6, 5x5
    __LAPACK_int m = shape == 0 ? 6 : shape == 1 ? 4 : 5, nn = shape == 0 ? 4 : shape == 1 ? 6 : 5, ld = m, mn = m < nn ? m : nn;
    double M0[36], M[36], s[6], U[36], VT[36], wk[1000]; __LAPACK_int lwk = 1000; for (int i = 0; i < 36; i++) M0[i] = rnd(); memcpy(M, M0, sizeof M);
    __LAPACK_int ldu = m, ldvt = nn;
    dgesvd_("A", "A", &m, &nn, M, &ld, s, U, &ldu, VT, &ldvt, wk, &lwk, &info);
    double rec[36], zr36[36] = {0}; for (int i = 0; i < m; i++) for (int j = 0; j < nn; j++) { double t = -M0[i + j * m]; for (int k = 0; k < mn; k++) t += U[i + k * m] * s[k] * VT[k + j * nn]; rec[i + j * m] = t; }
    check("dgesvd_recon", rec, zr36, m * nn, 1e-12);
    double oc[36], oz[36] = {0}; for (int a = 0; a < m; a++) for (int b = 0; b < m; b++) { double t = a == b ? -1 : 0; for (int i = 0; i < m; i++) t += U[i + a * m] * U[i + b * m]; oc[a + b * m] = t; }
    check("dgesvd_Uorth", oc, oz, m * m, 1e-12);
    for (int a = 0; a < nn; a++) for (int b = 0; b < nn; b++) { double t = a == b ? -1 : 0; for (int i = 0; i < nn; i++) t += VT[a + i * nn] * VT[b + i * nn]; oc[a + b * nn] = t; }
    check("dgesvd_VTorth", oc, oz, nn * nn, 1e-12);
    if (dumpmode) emitd("dgesvd_s", s, mn); }
  { // allgemeine Eigenwerte: Matrix mit reellen und komplexen Werten
    __LAPACK_int en = 5, el = 5; double E0[25], E[25], wr[5], wi[5], VL[25], VR[25], wk[400]; __LAPACK_int lwk = 400;
    for (int i = 0; i < 25; i++) E0[i] = rnd(); memcpy(E, E0, sizeof E);
    dgeev_("V", "V", &en, E, &el, wr, wi, VL, &en, VR, &en, wk, &lwk, &info);
    if (info) { fprintf(stderr, "FAIL dgeev info\n"); fails++; }
    double rmax = 0, lmax = 0;
    for (int j = 0; j < 5; j++) {
      if (wi[j] == 0) {
        for (int i = 0; i < 5; i++) { double s = -wr[j] * VR[i + j * 5], t = -wr[j] * VL[i + j * 5]; for (int k = 0; k < 5; k++) { s += E0[i + k * 5] * VR[k + j * 5]; t += E0[k + i * 5] * VL[k + j * 5]; } rmax = fmax(rmax, fabs(s)); lmax = fmax(lmax, fabs(t)); }
      } else if (wi[j] > 0) {
        for (int i = 0; i < 5; i++) {
          double _Complex v = VR[i + j * 5] + Ci * VR[i + (j + 1) * 5], s = -(wr[j] + Ci * wi[j]) * v;
          double _Complex u = VL[i + j * 5] + Ci * VL[i + (j + 1) * 5], t = 0;
          for (int k = 0; k < 5; k++) { s += E0[i + k * 5] * (VR[k + j * 5] + Ci * VR[k + (j + 1) * 5]); t += E0[k + i * 5] * (VL[k + j * 5] + Ci * VL[k + (j + 1) * 5]); }
          t -= conj(wr[j] + Ci * wi[j]) * u * 1.0; /* A^T u = conj(l) u  <=> u^H A = l u^H */
          rmax = fmax(rmax, cabs(s)); lmax = fmax(lmax, cabs(t));
        }
      }
    }
    if (rmax > 1e-9 || lmax > 1e-9) { fprintf(stderr, "FAIL dgeev Residuen rechts %g links %g\n", rmax, lmax); fails++; }
    double ws[10]; for (int i = 0; i < 5; i++) { ws[i] = wr[i]; ws[5 + i] = fabs(wi[i]); }
    // Reihenfolge ist nicht festgelegt: sortiert ausgeben
    for (int i = 0; i < 5; i++) for (int j = i + 1; j < 5; j++) if (ws[j] + 1e-3 * ws[5 + j] < ws[i] + 1e-3 * ws[5 + i]) { double t = ws[i]; ws[i] = ws[j]; ws[j] = t; t = ws[5 + i]; ws[5 + i] = ws[5 + j]; ws[5 + j] = t; }
    if (dumpmode) emitd("dgeev_vals_sorted", ws, 10); }
}

