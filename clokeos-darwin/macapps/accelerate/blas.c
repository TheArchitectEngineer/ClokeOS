// Accelerate.framework für ClokeOS: CBLAS mit 32-Bit-Ganzzahlen (klassische Namen).
#include <Accelerate/Accelerate.h>
#include "blas.h"

void cblas_scopy(const int N, const float *X, const int incX, float *Y, const int incY) {
  for (int i = 0; i < N; i++) Y[(long)i * incY] = X[(long)i * incX];
}
void cblas_dcopy(const int N, const double *X, const int incX, double *Y, const int incY) {
  for (int i = 0; i < N; i++) Y[(long)i * incY] = X[(long)i * incX];
}
float cblas_sdot(const int N, const float *X, const int incX, const float *Y, const int incY) {
  float s = 0; for (int i = 0; i < N; i++) s += X[(long)i * incX] * Y[(long)i * incY]; return s;
}
double cblas_ddot(const int N, const double *X, const int incX, const double *Y, const int incY) {
  double s = 0; for (int i = 0; i < N; i++) s += X[(long)i * incX] * Y[(long)i * incY]; return s;
}
void cblas_saxpy(const int N, const float a, const float *X, const int incX, float *Y, const int incY) {
  for (int i = 0; i < N; i++) Y[(long)i * incY] += a * X[(long)i * incX];
}
void cblas_daxpy(const int N, const double a, const double *X, const int incX, double *Y, const int incY) {
  for (int i = 0; i < N; i++) Y[(long)i * incY] += a * X[(long)i * incX];
}
void cblas_sscal(const int N, const float a, float *X, const int incX) { for (int i = 0; i < N; i++) X[(long)i * incX] *= a; }
void cblas_dscal(const int N, const double a, double *X, const int incX) { for (int i = 0; i < N; i++) X[(long)i * incX] *= a; }
float cblas_snrm2(const int N, const float *X, const int incX) { double s = 0; for (int i = 0; i < N; i++) { double v = X[(long)i * incX]; s += v * v; } return (float)sqrt(s); }
double cblas_dnrm2(const int N, const double *X, const int incX) { double s = 0; for (int i = 0; i < N; i++) { double v = X[(long)i * incX]; s += v * v; } return sqrt(s); }

void cblas_sgemv(const enum CBLAS_ORDER order, const enum CBLAS_TRANSPOSE ta, const int M, const int N, const float alpha,
                 const float *A, const int lda, const float *X, const int incX, const float beta, float *Y, const int incY) {
  gemv_float(order == CblasRowMajor, ta != CblasNoTrans, M, N, alpha, A, lda, X, incX, beta, Y, incY);
}
void cblas_dgemv(const enum CBLAS_ORDER order, const enum CBLAS_TRANSPOSE ta, const int M, const int N, const double alpha,
                 const double *A, const int lda, const double *X, const int incX, const double beta, double *Y, const int incY) {
  gemv_double(order == CblasRowMajor, ta != CblasNoTrans, M, N, alpha, A, lda, X, incX, beta, Y, incY);
}
void cblas_sgemm(const enum CBLAS_ORDER order, const enum CBLAS_TRANSPOSE ta, const enum CBLAS_TRANSPOSE tb, const int M, const int N,
                 const int K, const float alpha, const float *A, const int lda, const float *B, const int ldb, const float beta,
                 float *C, const int ldc) {
  gemm_float(order == CblasRowMajor, ta != CblasNoTrans, tb != CblasNoTrans, M, N, K, alpha, A, lda, B, ldb, beta, C, ldc);
}
void cblas_dgemm(const enum CBLAS_ORDER order, const enum CBLAS_TRANSPOSE ta, const enum CBLAS_TRANSPOSE tb, const int M, const int N,
                 const int K, const double alpha, const double *A, const int lda, const double *B, const int ldb, const double beta,
                 double *C, const int ldc) {
  gemm_double(order == CblasRowMajor, ta != CblasNoTrans, tb != CblasNoTrans, M, N, K, alpha, A, lda, B, ldb, beta, C, ldc);
}
