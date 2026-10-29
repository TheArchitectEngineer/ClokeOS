// Gemeinsame BLAS-Kerne (Spaltenreihenfolge, lange Indizes); von blas.c (32-Bit-ABI) und lapack.c (ILP64) benutzt.
#include <stddef.h>
#include <math.h>
#define NCOL 0
#define NROW 1

#define GEMV_BODY(T)                                                                                                   \
  static void gemv_##T(int rowmajor, int trans, long M, long N, T alpha, const T *A, long lda, const T *X, long incx, \
                       T beta, T *Y, long incy) {                                                                      \
    if (rowmajor) { long t = M; M = N; N = t; trans = !trans; }                                                        \
    /* nun Spaltenreihenfolge: A ist M x N, ld lda; y = alpha*op(A)*x + beta*y */                                      \
    long ylen = trans ? N : M, xlen = trans ? M : N;                                                                   \
    long iy = incy < 0 ? (1 - ylen) * incy : 0, ix0 = incx < 0 ? (1 - xlen) * incx : 0;                                \
    for (long i = 0; i < ylen; i++) { T *yp = &Y[iy + i * incy]; *yp = beta == 0 ? 0 : *yp * beta; }                   \
    if (alpha == 0) return;                                                                                            \
    if (!trans) {                                                                                                      \
      for (long j = 0; j < N; j++) {                                                                                   \
        T t = alpha * X[ix0 + j * incx];                                                                               \
        const T *a = A + j * lda;                                                                                      \
        if (incy == 1) { for (long i = 0; i < M; i++) Y[i] += t * a[i]; }                                              \
        else for (long i = 0; i < M; i++) Y[iy + i * incy] += t * a[i];                                                \
      }                                                                                                                \
    } else {                                                                                                           \
      for (long j = 0; j < N; j++) {                                                                                   \
        const T *a = A + j * lda; T s = 0;                                                                             \
        if (incx == 1) { for (long i = 0; i < M; i++) s += a[i] * X[i]; }                                              \
        else for (long i = 0; i < M; i++) s += a[i] * X[ix0 + i * incx];                                               \
        Y[iy + j * incy] += alpha * s;                                                                                 \
      }                                                                                                                \
    }                                                                                                                  \
  }

#define GEMM_BODY(T)                                                                                                   \
  static void gemm_##T(int rowmajor, int ta, int tb, long M, long N, long K, T alpha, const T *A, long lda, const T *B, \
                       long ldb, T beta, T *C, long ldc) {                                                             \
    if (rowmajor) {  /* C^T = B^T A^T in Spaltenreihenfolge */                                                         \
      const T *tp = A; A = B; B = tp; long tl = lda; lda = ldb; ldb = tl; int tt = ta; ta = tb; tb = tt; tl = M; M = N; N = tl; \
    }                                                                                                                  \
    for (long j = 0; j < N; j++) {                                                                                     \
      for (long i = 0; i < M; i++) C[i + j * ldc] = beta == 0 ? 0 : C[i + j * ldc] * beta;                             \
      for (long k = 0; k < K; k++) {                                                                                   \
        T b = alpha * (tb ? B[j + k * ldb] : B[k + j * ldb]);                                                          \
        if (!ta) { const T *a = A + k * lda; T *c = C + j * ldc; for (long i = 0; i < M; i++) c[i] += a[i] * b; }      \
        else for (long i = 0; i < M; i++) C[i + j * ldc] += A[k + i * lda] * b;                                        \
      }                                                                                                                \
    }                                                                                                                  \
  }
GEMV_BODY(float)
GEMV_BODY(double)
GEMM_BODY(float)
GEMM_BODY(double)
