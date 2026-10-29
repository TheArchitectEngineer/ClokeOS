// Accelerate.framework für ClokeOS: LAPACK/CBLAS in der "NEWLAPACK ILP64"-Form, die neue Mac-Programme
// importieren (Symbole name$NEWLAPACK$ILP64, 64-Bit-Ganzzahlen). Zeilen-/Spaltenreihenfolge wie Fortran
// (Spalten zuerst). Eigener Code, kleine Dichtfälle: LU, SVD (einseitiges Jacobi), Eigenwerte (QR auf
// komplexer Hessenberg-Form + inverse Iteration für die Vektoren).
#define ACCELERATE_NEW_LAPACK
#define ACCELERATE_LAPACK_ILP64
#include <Accelerate/Accelerate.h>
#include <complex.h>
#undef I
#include <stdlib.h>
#include <string.h>
#include "blas.h"

typedef long I;
typedef double _Complex Z;
double acc_cabs(Z);
Z acc_csqrt(Z);
#define cabs acc_cabs
#define csqrt acc_csqrt

void cblas_dgemv(const enum CBLAS_ORDER order, const enum CBLAS_TRANSPOSE ta, const I M, const I N, const double alpha,
                 const double *A, const I lda, const double *X, const I incX, const double beta, double *Y, const I incY) {
  gemv_double(order == CblasRowMajor, ta != CblasNoTrans, M, N, alpha, A, lda, X, incX, beta, Y, incY);
}

#define A_(i, j) a[(i) + (j) * lda]

// ---- LU mit Zeilenvertauschung (Spalten zuerst) ----
static I lu_d(I m, I n, double *a, I lda, I *ipiv) {
  I info = 0, mn = m < n ? m : n;
  for (I k = 0; k < mn; k++) {
    I p = k; double mx = fabs(A_(k, k));
    for (I i = k + 1; i < m; i++) if (fabs(A_(i, k)) > mx) { mx = fabs(A_(i, k)); p = i; }
    ipiv[k] = p + 1;
    if (A_(p, k) != 0) {
      if (p != k) for (I j = 0; j < n; j++) { double t = A_(k, j); A_(k, j) = A_(p, j); A_(p, j) = t; }
      double inv = 1.0 / A_(k, k);
      for (I i = k + 1; i < m; i++) A_(i, k) *= inv;
    } else if (!info) info = k + 1;
    for (I j = k + 1; j < n; j++) {
      double t = A_(k, j);
      if (t != 0) for (I i = k + 1; i < m; i++) A_(i, j) -= A_(i, k) * t;
    }
  }
  return info;
}
static void lusolve_d(int trans, I n, I nrhs, const double *a, I lda, const I *ipiv, double *b, I ldb) {
  for (I c = 0; c < nrhs; c++) {
    double *x = b + c * ldb;
    if (!trans) {
      for (I k = 0; k < n; k++) { I p = ipiv[k] - 1; if (p != k) { double t = x[k]; x[k] = x[p]; x[p] = t; } }
      for (I k = 0; k < n; k++) for (I i = k + 1; i < n; i++) x[i] -= A_(i, k) * x[k];
      for (I k = n - 1; k >= 0; k--) { x[k] /= A_(k, k); for (I i = 0; i < k; i++) x[i] -= A_(i, k) * x[k]; }
    } else {   // A^T x = b: U^T, L^T, dann Vertauschung rückwärts
      for (I k = 0; k < n; k++) { double s = x[k]; for (I i = 0; i < k; i++) s -= A_(i, k) * x[i]; x[k] = s / A_(k, k); }
      for (I k = n - 1; k >= 0; k--) { double s = x[k]; for (I i = k + 1; i < n; i++) s -= A_(i, k) * x[i]; x[k] = s; }
      for (I k = n - 1; k >= 0; k--) { I p = ipiv[k] - 1; if (p != k) { double t = x[k]; x[k] = x[p]; x[p] = t; } }
    }
  }
}

void dgetrf_(const I *m, const I *n, double *a, const I *lda, I *ipiv, I *info) { *info = lu_d(*m, *n, a, *lda, ipiv); }
void dgetrs_(const char *trans, const I *n, const I *nrhs, const double *a, const I *lda, const I *ipiv, double *b, const I *ldb, I *info) {
  *info = 0;
  lusolve_d(*trans == 'T' || *trans == 't' || *trans == 'C' || *trans == 'c', *n, *nrhs, a, *lda, ipiv, b, *ldb);
}
void dgesv_(const I *n, const I *nrhs, double *a, const I *lda, I *ipiv, double *b, const I *ldb, I *info) {
  *info = lu_d(*n, *n, a, *lda, ipiv);
  if (!*info) lusolve_d(0, *n, *nrhs, a, *lda, ipiv, b, *ldb);
}
void dgetri_(const I *n, double *a, const I *plda, const I *ipiv, double *work, const I *lwork, I *info) {
  I N = *n, lda = *plda; *info = 0;
  if (*lwork == -1) { work[0] = (double)(N > 1 ? N * N : 1); return; }
  for (I k = 0; k < N; k++) if (A_(k, k) == 0) { *info = k + 1; return; }
  double *x = calloc((size_t)N * N + 1, sizeof(double));
  if (!x) { *info = -1; return; }
  for (I i = 0; i < N; i++) x[i + i * N] = 1;
  lusolve_d(0, N, N, a, lda, ipiv, x, N);
  for (I j = 0; j < N; j++) for (I i = 0; i < N; i++) A_(i, j) = x[i + j * N];
  free(x);
}
// Symmetrisch-indefinit: ohne Bunch-Kaufman; die angegebene Dreiecksseite wird gespiegelt und per LU gelöst.
// A enthält danach die LU-Zerlegung, ipiv deren Vertauschungen (nicht die Bunch-Kaufman-Form).
void dsysv_(const char *uplo, const I *n, const I *nrhs, double *a, const I *plda, I *ipiv, double *b, const I *ldb,
            double *work, const I *lwork, I *info) {
  I N = *n, lda = *plda; *info = 0;
  if (*lwork == -1) { work[0] = 1; return; }
  int up = *uplo == 'U' || *uplo == 'u';
  for (I j = 0; j < N; j++) for (I i = j + 1; i < N; i++) { if (up) A_(i, j) = A_(j, i); else A_(j, i) = A_(i, j); }
  *info = lu_d(N, N, a, lda, ipiv);
  if (!*info) lusolve_d(0, N, *nrhs, a, lda, ipiv, b, *ldb);
}

// ---- komplexes Lösen A x = b (zcgesv): LU in doppelter Genauigkeit ----
void zcgesv_(const I *n, const I *nrhs, Z *a, const I *lda, I *ipiv, const Z *b, const I *ldb, Z *x, const I *ldx,
             Z *work, __LAPACK_float_complex *swork, double *rwork, I *iter, I *info) {
  I N = *n, ld = *lda; *info = 0; *iter = -1;   // wie der Rückfall von LAPACK: reine Doppelgenauigkeit
  for (I c = 0; c < *nrhs; c++) for (I i = 0; i < N; i++) x[i + c * *ldx] = b[i + c * *ldb];
  for (I k = 0; k < N; k++) {
    I p = k; double mx = cabs(a[k + k * ld]);
    for (I i = k + 1; i < N; i++) if (cabs(a[i + k * ld]) > mx) { mx = cabs(a[i + k * ld]); p = i; }
    ipiv[k] = p + 1;
    if (mx == 0) { if (!*info) *info = k + 1; continue; }
    if (p != k) for (I j = 0; j < N; j++) { Z t = a[k + j * ld]; a[k + j * ld] = a[p + j * ld]; a[p + j * ld] = t; }
    for (I i = k + 1; i < N; i++) a[i + k * ld] /= a[k + k * ld];
    for (I j = k + 1; j < N; j++) { Z t = a[k + j * ld]; for (I i = k + 1; i < N; i++) a[i + j * ld] -= a[i + k * ld] * t; }
  }
  if (*info) return;
  for (I c = 0; c < *nrhs; c++) {
    Z *v = x + c * *ldx;
    for (I k = 0; k < N; k++) { I p = ipiv[k] - 1; if (p != k) { Z t = v[k]; v[k] = v[p]; v[p] = t; } }
    for (I k = 0; k < N; k++) for (I i = k + 1; i < N; i++) v[i] -= a[i + k * ld] * v[k];
    for (I k = N - 1; k >= 0; k--) { v[k] /= a[k + k * ld]; for (I i = 0; i < k; i++) v[i] -= a[i + k * ld] * v[k]; }
  }
}

// ---- SVD durch einseitiges Jacobi-Verfahren ----
// A (m x n, m >= n) -> U (m x n, Spalten orthonormal soweit s > 0), s absteigend, V (n x n)
static void svd_tall(I m, I n, const double *a, I lda, double *U, double *s, double *V) {
  for (I j = 0; j < n; j++) for (I i = 0; i < m; i++) U[i + j * m] = A_(i, j);
  for (I j = 0; j < n; j++) for (I i = 0; i < n; i++) V[i + j * n] = i == j;
  for (int sweep = 0; sweep < 60; sweep++) {
    int rotated = 0;
    for (I p = 0; p < n - 1; p++) for (I q = p + 1; q < n; q++) {
      double al = 0, be = 0, ga = 0;
      for (I i = 0; i < m; i++) { double x = U[i + p * m], y = U[i + q * m]; al += x * x; be += y * y; ga += x * y; }
      if (ga == 0 || fabs(ga) <= 1e-15 * sqrt(al * be)) continue;
      rotated = 1;
      double zeta = (be - al) / (2 * ga), t = (zeta >= 0 ? 1 : -1) / (fabs(zeta) + sqrt(1 + zeta * zeta));
      double c = 1 / sqrt(1 + t * t), sn = c * t;
      for (I i = 0; i < m; i++) { double x = U[i + p * m], y = U[i + q * m]; U[i + p * m] = c * x - sn * y; U[i + q * m] = sn * x + c * y; }
      for (I i = 0; i < n; i++) { double x = V[i + p * n], y = V[i + q * n]; V[i + p * n] = c * x - sn * y; V[i + q * n] = sn * x + c * y; }
    }
    if (!rotated) break;
  }
  for (I j = 0; j < n; j++) {
    double nr = 0; for (I i = 0; i < m; i++) nr += U[i + j * m] * U[i + j * m];
    s[j] = sqrt(nr);
  }
  // absteigend sortieren (Einfügesortierung der Spalten)
  for (I j = 1; j < n; j++) for (I k = j; k > 0 && s[k] > s[k - 1]; k--) {
    double t = s[k]; s[k] = s[k - 1]; s[k - 1] = t;
    for (I i = 0; i < m; i++) { t = U[i + k * m]; U[i + k * m] = U[i + (k - 1) * m]; U[i + (k - 1) * m] = t; }
    for (I i = 0; i < n; i++) { t = V[i + k * n]; V[i + k * n] = V[i + (k - 1) * n]; V[i + (k - 1) * n] = t; }
  }
  double smax = n ? s[0] : 0;
  for (I j = 0; j < n; j++) if (s[j] > smax * 1e-300 && s[j] > 0) for (I i = 0; i < m; i++) U[i + j * m] /= s[j];
  else for (I i = 0; i < m; i++) U[i + j * m] = 0;
}
// ergänzt Spalten ab `have` von Q (rows x want) zu einer Orthonormalbasis (Gram-Schmidt mit Einheitsvektoren);
// Spalten, die nur Null sind, zählen als fehlend (have wird dafür nicht benutzt: wir prüfen die Norm)
static void complete_basis(I rows, I want, double *Q) {
  I e = 0;
  for (I j = 0; j < want; j++) {
    double nr = 0; for (I i = 0; i < rows; i++) nr += Q[i + j * rows] * Q[i + j * rows];
    if (nr > 0.5) continue;
    for (; e < rows; e++) {
      for (I i = 0; i < rows; i++) Q[i + j * rows] = i == e;
      for (int rep = 0; rep < 2; rep++) for (I k = 0; k < want; k++) {
        if (k == j) continue;
        double d = 0; for (I i = 0; i < rows; i++) d += Q[i + k * rows] * Q[i + j * rows];
        for (I i = 0; i < rows; i++) Q[i + j * rows] -= d * Q[i + k * rows];
      }
      double r = 0; for (I i = 0; i < rows; i++) r += Q[i + j * rows] * Q[i + j * rows];
      e++;
      if (r > 1e-8) { r = sqrt(r); for (I i = 0; i < rows; i++) Q[i + j * rows] /= r; break; }
    }
  }
}

void dgesvd_(const char *jobu, const char *jobvt, const I *pm, const I *pn, double *a, const I *plda, double *s, double *u,
             const I *pldu, double *vt, const I *pldvt, double *work, const I *lwork, I *info) {
  I m = *pm, n = *pn, lda = *plda, mn = m < n ? m : n; *info = 0;
  if (*lwork == -1) { work[0] = (double)(m * n + n * n + m * m + 1); return; }
  char ju = *jobu & ~0x20, jv = *jobvt & ~0x20;
  if (mn == 0) return;
  int tall = m >= n;
  I r = tall ? m : n, c = tall ? n : m;   // r x c mit r >= c
  double *B = malloc(sizeof(double) * (size_t)(r * c + c * c + 1)), *Vv = B + r * c, *At = NULL;
  const double *src = a; I ld = lda;
  if (!tall) {   // A^T zerlegen
    At = malloc(sizeof(double) * (size_t)(m * n + 1));
    for (I j = 0; j < n; j++) for (I i = 0; i < m; i++) At[j + i * n] = A_(i, j);
    src = At; ld = n;
  }
  double *Ufull = NULL;
  const double *a_ = src; (void)a_;
  {
    double *saved = malloc(sizeof(double) * (size_t)(r * c + 1));
    for (I j = 0; j < c; j++) for (I i = 0; i < r; i++) saved[i + j * r] = src[i + j * ld];
    svd_tall(r, c, saved, r, B, s, Vv);
    free(saved);
  }
  // Linke Matrix des gewählten Problems: tall -> U = B (m x n), VT = Vv^T ; wide -> U = Vv (m x m), VT = B^T (m x n)
  if (tall) {
    if (ju == 'A') {
      Ufull = calloc((size_t)(m * m) + 1, sizeof(double));
      for (I j = 0; j < n; j++) for (I i = 0; i < m; i++) Ufull[i + j * m] = B[i + j * m];
      complete_basis(m, m, Ufull);
      for (I j = 0; j < m; j++) for (I i = 0; i < m; i++) u[i + j * *pldu] = Ufull[i + j * m];
      free(Ufull);
    } else if (ju == 'S') for (I j = 0; j < n; j++) for (I i = 0; i < m; i++) u[i + j * *pldu] = B[i + j * m];
    else if (ju == 'O') for (I j = 0; j < n; j++) for (I i = 0; i < m; i++) A_(i, j) = B[i + j * m];
    if (jv == 'A' || jv == 'S') for (I j = 0; j < n; j++) for (I i = 0; i < n; i++) vt[i + j * *pldvt] = Vv[j + i * n];
    else if (jv == 'O') for (I j = 0; j < n; j++) for (I i = 0; i < n; i++) A_(i, j) = Vv[j + i * n];
  } else {
    if (ju == 'A' || ju == 'S') for (I j = 0; j < m; j++) for (I i = 0; i < m; i++) u[i + j * *pldu] = Vv[i + j * m];
    else if (ju == 'O') for (I j = 0; j < m; j++) for (I i = 0; i < m; i++) A_(i, j) = Vv[i + j * m];
    if (jv == 'A') {
      Ufull = calloc((size_t)(n * n) + 1, sizeof(double));
      for (I j = 0; j < m; j++) for (I i = 0; i < n; i++) Ufull[i + j * n] = B[i + j * n];
      complete_basis(n, n, Ufull);
      for (I j = 0; j < n; j++) for (I i = 0; i < n; i++) vt[j + i * *pldvt] = Ufull[i + j * n];
      free(Ufull);
    } else if (jv == 'S') for (I j = 0; j < m; j++) for (I i = 0; i < n; i++) vt[j + i * *pldvt] = B[i + j * n];
    else if (jv == 'O') for (I j = 0; j < m; j++) for (I i = 0; i < n; i++) A_(j, i) = B[i + j * n];
  }
  free(B); free(At);
}

// ---- Allgemeine Eigenwertaufgabe ----
// Eigenwerte: komplexe Hessenberg-QR mit Wilkinson-Verschiebung auf einer Kopie von A (nur Werte).
static void eigvals(I n, const double *a, I lda, Z *w) {
  Z *H = malloc(sizeof(Z) * (size_t)(n * n + 1));
  for (I j = 0; j < n; j++) for (I i = 0; i < n; i++) H[i + j * n] = A_(i, j);
#define H_(i, j) H[(i) + (j) * n]
  for (I k = 0; k + 2 < n; k++) {   // Householder auf Hessenberg-Form
    double nr = 0; for (I i = k + 1; i < n; i++) nr += creal(H_(i, k) * conj(H_(i, k)));
    nr = sqrt(nr); if (nr == 0) continue;
    Z x0 = H_(k + 1, k), ph = cabs(x0) == 0 ? 1 : x0 / cabs(x0);
    Z *v = malloc(sizeof(Z) * (size_t)n);
    for (I i = k + 1; i < n; i++) v[i] = H_(i, k);
    v[k + 1] += ph * nr;
    double vn = 0; for (I i = k + 1; i < n; i++) vn += creal(v[i] * conj(v[i]));
    if (vn > 0) {
      for (I j = 0; j < n; j++) { Z d = 0; for (I i = k + 1; i < n; i++) d += conj(v[i]) * H_(i, j); d *= 2 / vn; for (I i = k + 1; i < n; i++) H_(i, j) -= v[i] * d; }
      for (I i = 0; i < n; i++) { Z d = 0; for (I j = k + 1; j < n; j++) d += H_(i, j) * v[j]; d *= 2 / vn; for (I j = k + 1; j < n; j++) H_(i, j) -= d * conj(v[j]); }
    }
    free(v);
  }
  I hi = n - 1; int its = 0;
  while (hi >= 0) {
    if (hi == 0) { w[0] = H_(0, 0); break; }
    I l = hi;   // kleinstes l, so dass H(l, l-1) vernachlässigbar
    while (l > 0) {
      double s = cabs(H_(l - 1, l - 1)) + cabs(H_(l, l));
      if (s == 0) s = 1;
      if (cabs(H_(l, l - 1)) < 1e-16 * s) { H_(l, l - 1) = 0; break; }
      l--;
    }
    if (l == hi) { w[hi] = H_(hi, hi); hi--; its = 0; continue; }
    its++;
    Z a_ = H_(hi - 1, hi - 1), b_ = H_(hi - 1, hi), c_ = H_(hi, hi - 1), d_ = H_(hi, hi);
    Z tr = a_ + d_, det = a_ * d_ - b_ * c_, disc = csqrt(tr * tr / 4 - det);
    Z l1 = tr / 2 + disc, l2 = tr / 2 - disc;
    Z mu = cabs(l1 - d_) < cabs(l2 - d_) ? l1 : l2;
    if (its % 11 == 10) mu = d_ + cabs(H_(hi, hi - 1)) * (1.0 + 0.7 * _Complex_I);   // außerordentliche Verschiebung
    // QR-Schritt auf [l, hi] mit Givens-Drehungen: H - mu = QR, H = RQ + mu
    I len = hi - l + 1;
    Z *cs = malloc(sizeof(Z) * 2 * (size_t)len); double *cr = malloc(sizeof(double) * (size_t)len);
    for (I i = l; i <= hi; i++) H_(i, i) -= mu;
    for (I k = l; k < hi; k++) {
      Z f = H_(k, k), g = H_(k + 1, k); double nr = sqrt(creal(f * conj(f) + g * conj(g)));
      Z c = nr == 0 ? 1 : f / nr, s = nr == 0 ? 0 : g / nr;   // G = [conj(c) conj(s); -s c]
      cr[k - l] = 0; cs[2 * (k - l)] = c; cs[2 * (k - l) + 1] = s;
      for (I j = k; j <= hi; j++) { Z x = H_(k, j), y = H_(k + 1, j); H_(k, j) = conj(c) * x + conj(s) * y; H_(k + 1, j) = -s * x + c * y; }
    }
    for (I k = l; k < hi; k++) {
      Z c = cs[2 * (k - l)], s = cs[2 * (k - l) + 1];
      for (I i = l; i <= (k + 2 < hi ? k + 2 : hi); i++) { Z x = H_(i, k), y = H_(i, k + 1); H_(i, k) = x * c + y * s; H_(i, k + 1) = -x * conj(s) + y * conj(c); }
    }
    for (I i = l; i <= hi; i++) H_(i, i) += mu;
    free(cs); free(cr);
    if (its > 300) { for (I i = l; i <= hi; i++) w[i] = H_(i, i); hi = l - 1; }   // unvollständig; praktisch nie
  }
  free(H);
#undef H_
}

// Eigenvektor von M (n x n reell, Spalten zuerst) zum Eigenwert lam durch inverse Iteration;
// prev: bereits gefundene Vektoren desselben Eigenwertclusters (np Stück), gegen die orthogonalisiert wird.
static void eigvec(I n, const double *M, I ldm, Z lam, Z *x, Z **prev, I np, double anorm) {
  Z *L = malloc(sizeof(Z) * (size_t)(n * n + 1)); I *piv = malloc(sizeof(I) * (size_t)(n + 1));
  Z shift = lam + (anorm > 0 ? anorm : 1) * 1e-10;
  for (I j = 0; j < n; j++) for (I i = 0; i < n; i++) L[i + j * n] = M[i + j * ldm] - (i == j ? shift : 0);
  for (I k = 0; k < n; k++) {   // LU
    I p = k; double mx = cabs(L[k + k * n]);
    for (I i = k + 1; i < n; i++) if (cabs(L[i + k * n]) > mx) { mx = cabs(L[i + k * n]); p = i; }
    piv[k] = p;
    if (mx == 0) { L[k + k * n] = 1e-300; mx = 1e-300; }
    if (p != k) for (I j = 0; j < n; j++) { Z t = L[k + j * n]; L[k + j * n] = L[p + j * n]; L[p + j * n] = t; }
    for (I i = k + 1; i < n; i++) L[i + k * n] /= L[k + k * n];
    for (I j = k + 1; j < n; j++) { Z t = L[k + j * n]; for (I i = k + 1; i < n; i++) L[i + j * n] -= L[i + k * n] * t; }
  }
  for (I i = 0; i < n; i++) x[i] = 1.0 + 0.37 * (double)((i * 7 + np * 3) % 5) - 0.2 * _Complex_I * (double)((i + np) % 3);
  for (int it = 0; it < 4; it++) {
    for (I q = 0; q < np; q++) {   // gegen frühere Vektoren orthogonalisieren
      Z d = 0; for (I i = 0; i < n; i++) d += conj(prev[q][i]) * x[i];
      for (I i = 0; i < n; i++) x[i] -= d * prev[q][i];
    }
    for (I k = 0; k < n; k++) { I p = piv[k]; if (p != k) { Z t = x[k]; x[k] = x[p]; x[p] = t; } }
    for (I k = 0; k < n; k++) for (I i = k + 1; i < n; i++) x[i] -= L[i + k * n] * x[k];
    for (I k = n - 1; k >= 0; k--) { x[k] /= L[k + k * n]; for (I i = 0; i < k; i++) x[i] -= L[i + k * n] * x[k]; }
    double nr = 0; for (I i = 0; i < n; i++) nr += creal(x[i] * conj(x[i]));
    nr = sqrt(nr); if (nr == 0 || nr != nr) nr = 1;
    for (I i = 0; i < n; i++) x[i] /= nr;
  }
  for (I q = 0; q < np; q++) {
    Z d = 0; for (I i = 0; i < n; i++) d += conj(prev[q][i]) * x[i];
    for (I i = 0; i < n; i++) x[i] -= d * prev[q][i];
  }
  double nr = 0; for (I i = 0; i < n; i++) nr += creal(x[i] * conj(x[i]));
  nr = sqrt(nr); if (nr > 0) for (I i = 0; i < n; i++) x[i] /= nr;
  free(L); free(piv);
}

void dgeev_(const char *jobvl, const char *jobvr, const I *pn, double *a, const I *plda, double *wr, double *wi, double *vl,
            const I *pldvl, double *vr, const I *pldvr, double *work, const I *lwork, I *info) {
  I n = *pn, lda = *plda; *info = 0;
  if (*lwork == -1) { work[0] = (double)(4 * n + 1); return; }
  if (n == 0) return;
  int wantl = *jobvl == 'V' || *jobvl == 'v', wantr = *jobvr == 'V' || *jobvr == 'v';
  double anorm = 0; for (I j = 0; j < n; j++) for (I i = 0; i < n; i++) if (fabs(A_(i, j)) > anorm) anorm = fabs(A_(i, j));
  Z *w = malloc(sizeof(Z) * (size_t)n);
  eigvals(n, a, lda, w);
  // reelle und komplex konjugierte Paare herstellen
  double tol = 1e-9 * (anorm > 0 ? anorm : 1);
  char *st = calloc((size_t)n, 1);   // 0 offen, 1 fertig
  I *ord = malloc(sizeof(I) * (size_t)n); Z *wo = malloc(sizeof(Z) * (size_t)n); I cnt = 0;
  for (I i = 0; i < n; i++) {
    if (st[i]) continue;
    if (fabs(cimag(w[i])) <= tol) { wo[cnt++] = creal(w[i]); st[i] = 1; continue; }
    I best = -1; double bd = 1e300;
    for (I j = 0; j < n; j++) if (!st[j] && j != i && cimag(w[j]) * cimag(w[i]) < 0) { double d = cabs(w[j] - conj(w[i])); if (d < bd) { bd = d; best = j; } }
    st[i] = 1; if (best >= 0) st[best] = 1;
    Z p = best >= 0 ? (w[i] + conj(w[best])) / 2 : w[i];
    if (cimag(p) < 0) p = conj(p);
    wo[cnt++] = p; wo[cnt++] = conj(p);
  }
  (void)ord;
  for (I i = 0; i < n; i++) { wr[i] = creal(wo[i]); wi[i] = cimag(wo[i]); }
  Z **prevR = malloc(sizeof(Z *) * (size_t)n), **prevL = malloc(sizeof(Z *) * (size_t)n);
  double *At = NULL;
  if (wantl) { At = malloc(sizeof(double) * (size_t)(n * n)); for (I j = 0; j < n; j++) for (I i = 0; i < n; i++) At[i + j * n] = A_(j, i); }
  for (int side = 0; side < 2; side++) {
    if (side == 0 ? !wantr : !wantl) continue;
    double *out = side == 0 ? vr : vl; I ldo = side == 0 ? *pldvr : *pldvl;
    const double *M = side == 0 ? a : At; I ldm = side == 0 ? lda : n;
    Z **prev = side == 0 ? prevR : prevL;
    I np = 0; Z cl = 0;
    for (I j = 0; j < n; j++) {
      Z lam = wo[j];
      if (cimag(lam) < 0) continue;   // zweite Hälfte eines Paares wird aus der ersten abgeleitet
      Z lam_eff = side == 0 ? lam : conj(lam);
      if (j == 0 || cabs(lam - cl) > 1e-6 * (anorm > 0 ? anorm : 1)) np = 0;
      cl = lam;
      Z *x = malloc(sizeof(Z) * (size_t)n);
      eigvec(n, M, ldm, lam_eff, x, prev, np, anorm);
      prev[np++] = x;
      if (side == 1) for (I i = 0; i < n; i++) x[i] = conj(x[i]) == x[i] ? x[i] : x[i];   // x bleibt Vektor zu conj(lam) für A^T
      if (cimag(lam) == 0) {
        I big = 0; for (I i = 1; i < n; i++) if (cabs(x[i]) > cabs(x[big])) big = i;
        Z ph = cabs(x[big]) == 0 ? 1 : conj(x[big]) / cabs(x[big]); (void)ph;
        // reeller Vektor: Phase so wählen, dass die größte Komponente reell wird
        Z ph2 = x[big] / cabs(x[big]);
        for (I i = 0; i < n; i++) out[i + j * ldo] = creal(x[i] / ph2);
      } else {
        // Linksvektor u erfüllt A^T u = conj(lam) u; x ist genau dieser, also u^H A = lam u^H
        I big = 0; for (I i = 1; i < n; i++) if (cabs(x[i]) > cabs(x[big])) big = i;
        Z ph = x[big] / cabs(x[big]);
        for (I i = 0; i < n; i++) { Z v = x[i] / ph; out[i + j * ldo] = creal(v); out[i + (j + 1) * ldo] = cimag(v); }
        // Paar j, j+1: LAPACK speichert Re und Im des Vektors zu wi[j] > 0
        if (side == 1) {   // Linksvektor zu lam ist u = x (A^T x = conj(lam) x), gespeichert wie definiert
        }
        j++;   // j+1 ist der konjugierte Partner (Position folgt aus der Paarung oben)
      }
    }
    for (I q = 0; q < np; q++) free(prev[q]);
  }
  free(At); free(prevR); free(prevL); free(w); free(wo); free(ord); free(st);
}
