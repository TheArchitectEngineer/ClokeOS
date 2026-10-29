// Vorlage für vDSP-Funktionen in einer Genauigkeit; wird aus vdsp.c zweimal eingebunden
// (T = float ohne Suffix, T = double mit Suffix D). Eigener Code, nur die dokumentierten
// Bedeutungen der vDSP-Funktionen aus den SDK-Headern.
#define FN(x) CAT(vDSP_##x, SUF)
#define LOOP for (vDSP_Length n = 0; n < N; n++)

void FN(vadd)(const T *A, vDSP_Stride IA, const T *B, vDSP_Stride IB, T *C, vDSP_Stride IC, vDSP_Length N) {
  if (IA == 1 && IB == 1 && IC == 1) { for (vDSP_Length n = 0; n < N; n++) C[n] = A[n] + B[n]; return; }
  LOOP C[n * IC] = A[n * IA] + B[n * IB];
}
void FN(vsub)(const T *A, vDSP_Stride IA, const T *B, vDSP_Stride IB, T *C, vDSP_Stride IC, vDSP_Length N) {
  if (IA == 1 && IB == 1 && IC == 1) { for (vDSP_Length n = 0; n < N; n++) C[n] = B[n] - A[n]; return; }
  LOOP C[n * IC] = B[n * IB] - A[n * IA];
}
void FN(vmul)(const T *A, vDSP_Stride IA, const T *B, vDSP_Stride IB, T *C, vDSP_Stride IC, vDSP_Length N) {
  if (IA == 1 && IB == 1 && IC == 1) { for (vDSP_Length n = 0; n < N; n++) C[n] = A[n] * B[n]; return; }
  LOOP C[n * IC] = A[n * IA] * B[n * IB];
}
void FN(vdiv)(const T *B, vDSP_Stride IB, const T *A, vDSP_Stride IA, T *C, vDSP_Stride IC, vDSP_Length N) {
  if (IA == 1 && IB == 1 && IC == 1) { for (vDSP_Length n = 0; n < N; n++) C[n] = A[n] / B[n]; return; }
  LOOP C[n * IC] = A[n * IA] / B[n * IB];
}
void FN(vsadd)(const T *A, vDSP_Stride IA, const T *B, T *C, vDSP_Stride IC, vDSP_Length N) {
  T b = *B; LOOP C[n * IC] = A[n * IA] + b;
}
void FN(vsmul)(const T *A, vDSP_Stride IA, const T *B, T *C, vDSP_Stride IC, vDSP_Length N) {
  T b = *B;
  if (IA == 1 && IC == 1) { for (vDSP_Length n = 0; n < N; n++) C[n] = A[n] * b; return; }
  LOOP C[n * IC] = A[n * IA] * b;
}
void FN(vsdiv)(const T *A, vDSP_Stride IA, const T *B, T *C, vDSP_Stride IC, vDSP_Length N) {
  T b = *B; LOOP C[n * IC] = A[n * IA] / b;
}
void FN(svdiv)(const T *A, const T *B, vDSP_Stride IB, T *C, vDSP_Stride IC, vDSP_Length N) {
  T a = *A; LOOP C[n * IC] = a / B[n * IB];
}
void FN(vsma)(const T *A, vDSP_Stride IA, const T *B, const T *C, vDSP_Stride IC, T *D, vDSP_Stride ID, vDSP_Length N) {
  T b = *B; LOOP D[n * ID] = A[n * IA] * b + C[n * IC];
}
void FN(vsmsa)(const T *A, vDSP_Stride IA, const T *B, const T *C, T *D, vDSP_Stride ID, vDSP_Length N) {
  T b = *B, c = *C; LOOP D[n * ID] = A[n * IA] * b + c;
}
void FN(vsmsma)(const T *A, vDSP_Stride IA, const T *B, const T *C, vDSP_Stride IC, const T *D, T *E, vDSP_Stride IE, vDSP_Length N) {
  T b = *B, d = *D; LOOP E[n * IE] = A[n * IA] * b + C[n * IC] * d;
}
void FN(vma)(const T *A, vDSP_Stride IA, const T *B, vDSP_Stride IB, const T *C, vDSP_Stride IC, T *D, vDSP_Stride ID, vDSP_Length N) {
  LOOP D[n * ID] = A[n * IA] * B[n * IB] + C[n * IC];
}
void FN(vasm)(const T *A, vDSP_Stride IA, const T *B, vDSP_Stride IB, const T *C, T *D, vDSP_Stride ID, vDSP_Length N) {
  T c = *C; LOOP D[n * ID] = (A[n * IA] + B[n * IB]) * c;
}
void FN(vsbsm)(const T *A, vDSP_Stride IA, const T *B, vDSP_Stride IB, const T *C, T *D, vDSP_Stride ID, vDSP_Length N) {
  T c = *C; LOOP D[n * ID] = (A[n * IA] - B[n * IB]) * c;
}
void FN(vneg)(const T *A, vDSP_Stride IA, T *C, vDSP_Stride IC, vDSP_Length N) { LOOP C[n * IC] = -A[n * IA]; }
void FN(vabs)(const T *A, vDSP_Stride IA, T *C, vDSP_Stride IC, vDSP_Length N) { LOOP C[n * IC] = FABS(A[n * IA]); }
void FN(vsq)(const T *A, vDSP_Stride IA, T *C, vDSP_Stride IC, vDSP_Length N) { LOOP C[n * IC] = A[n * IA] * A[n * IA]; }
void FN(vclr)(T *C, vDSP_Stride IC, vDSP_Length N) { LOOP C[n * IC] = 0; }
void FN(vfill)(const T *A, T *C, vDSP_Stride IC, vDSP_Length N) { T a = *A; LOOP C[n * IC] = a; }
void FN(vrvrs)(T *C, vDSP_Stride IC, vDSP_Length N) {
  for (vDSP_Length i = 0, j = N; i + 1 < j; i++, j--) { T t = C[i * IC]; C[i * IC] = C[(j - 1) * IC]; C[(j - 1) * IC] = t; }
}
void FN(vramp)(const T *A, const T *B, T *C, vDSP_Stride IC, vDSP_Length N) {
  T a = *A, b = *B; LOOP C[n * IC] = a + (T)n * b;
}
void FN(vgen)(const T *A, const T *B, T *C, vDSP_Stride IC, vDSP_Length N) {
  T a = *A, b = *B;
  if (N == 1) { C[0] = a; return; }
  T d = (b - a) / (T)(N - 1);
  LOOP C[n * IC] = a + (T)n * d;
  if (N > 1) C[(N - 1) * IC] = b;
}
void FN(vclip)(const T *A, vDSP_Stride IA, const T *B, const T *C, T *D, vDSP_Stride ID, vDSP_Length N) {
  T lo = *B, hi = *C;
  LOOP { T x = A[n * IA]; D[n * ID] = x < lo ? lo : (x > hi ? hi : x); }
}
void FN(vclipc)(const T *A, vDSP_Stride IA, const T *B, const T *C, T *D, vDSP_Stride ID, vDSP_Length N, vDSP_Length *NLOW, vDSP_Length *NHI) {
  T lo = *B, hi = *C; vDSP_Length nl = 0, nh = 0;
  LOOP { T x = A[n * IA]; if (x < lo) { x = lo; nl++; } else if (x > hi) { x = hi; nh++; } D[n * ID] = x; }
  *NLOW = nl; *NHI = nh;
}
void FN(vlim)(const T *A, vDSP_Stride IA, const T *B, const T *C, T *D, vDSP_Stride ID, vDSP_Length N) {
  T b = *B, c = *C; LOOP D[n * ID] = A[n * IA] >= b ? c : -c;
}
void FN(vthr)(const T *A, vDSP_Stride IA, const T *B, T *C, vDSP_Stride IC, vDSP_Length N) {
  T b = *B; LOOP { T x = A[n * IA]; C[n * IC] = x >= b ? x : b; }
}
void FN(vthres)(const T *A, vDSP_Stride IA, const T *B, T *C, vDSP_Stride IC, vDSP_Length N) {
  T b = *B; LOOP { T x = A[n * IA]; C[n * IC] = x >= b ? x : 0; }
}
void FN(vmax)(const T *A, vDSP_Stride IA, const T *B, vDSP_Stride IB, T *C, vDSP_Stride IC, vDSP_Length N) {
  LOOP { T a = A[n * IA], b = B[n * IB]; C[n * IC] = a > b ? a : b; }
}
void FN(vmin)(const T *A, vDSP_Stride IA, const T *B, vDSP_Stride IB, T *C, vDSP_Stride IC, vDSP_Length N) {
  LOOP { T a = A[n * IA], b = B[n * IB]; C[n * IC] = a < b ? a : b; }
}
void FN(vmaxmg)(const T *A, vDSP_Stride IA, const T *B, vDSP_Stride IB, T *C, vDSP_Stride IC, vDSP_Length N) {
  LOOP { T a = FABS(A[n * IA]), b = FABS(B[n * IB]); C[n * IC] = a > b ? a : b; }
}
void FN(vdist)(const T *A, vDSP_Stride IA, const T *B, vDSP_Stride IB, T *C, vDSP_Stride IC, vDSP_Length N) {
  LOOP { T a = A[n * IA], b = B[n * IB]; C[n * IC] = SQRT(a * a + b * b); }
}
void FN(vpythg)(const T *A, vDSP_Stride IA, const T *B, vDSP_Stride IB, const T *C, vDSP_Stride IC, const T *D, vDSP_Stride ID, T *E, vDSP_Stride IE, vDSP_Length N) {
  LOOP { T x = A[n * IA] - C[n * IC], y = B[n * IB] - D[n * ID]; E[n * IE] = SQRT(x * x + y * y); }
}
void FN(vdbcon)(const T *A, vDSP_Stride IA, const T *B, T *C, vDSP_Stride IC, vDSP_Length N, unsigned int F) {
  T b = *B, k = F ? 20 : 10; LOOP C[n * IC] = k * LOG10(A[n * IA] / b);
}
void FN(vfrac)(const T *A, vDSP_Stride IA, T *C, vDSP_Stride IC, vDSP_Length N) {
  LOOP { T x = A[n * IA]; C[n * IC] = x - TRUNC(x); }
}
void FN(vpoly)(const T *A, vDSP_Stride IA, const T *B, vDSP_Stride IB, T *C, vDSP_Stride IC, vDSP_Length N, vDSP_Length P) {
  LOOP { T x = B[n * IB], y = A[0]; for (vDSP_Length k = 1; k <= P; k++) y = y * x + A[k * IA]; C[n * IC] = y; }
}

// Summen und Extrema
void FN(sve)(const T *A, vDSP_Stride IA, T *C, vDSP_Length N) {
  T s = 0;
  if (IA == 1) { for (vDSP_Length n = 0; n < N; n++) s += A[n]; } else LOOP s += A[n * IA];
  *C = s;
}
void FN(svemg)(const T *A, vDSP_Stride IA, T *C, vDSP_Length N) { T s = 0; LOOP s += FABS(A[n * IA]); *C = s; }
void FN(svesq)(const T *A, vDSP_Stride IA, T *C, vDSP_Length N) { T s = 0; LOOP s += A[n * IA] * A[n * IA]; *C = s; }
void FN(meanv)(const T *A, vDSP_Stride IA, T *C, vDSP_Length N) { T s = 0; LOOP s += A[n * IA]; *C = N ? s / (T)N : 0; }
void FN(measqv)(const T *A, vDSP_Stride IA, T *C, vDSP_Length N) { T s = 0; LOOP s += A[n * IA] * A[n * IA]; *C = N ? s / (T)N : 0; }
void FN(rmsqv)(const T *A, vDSP_Stride IA, T *C, vDSP_Length N) { T s = 0; LOOP s += A[n * IA] * A[n * IA]; *C = N ? SQRT(s / (T)N) : 0; }
void FN(dotpr)(const T *A, vDSP_Stride IA, const T *B, vDSP_Stride IB, T *C, vDSP_Length N) {
  T s = 0;
  if (IA == 1 && IB == 1) { for (vDSP_Length n = 0; n < N; n++) s += A[n] * B[n]; } else LOOP s += A[n * IA] * B[n * IB];
  *C = s;
}
void FN(distancesq)(const T *A, vDSP_Stride IA, const T *B, vDSP_Stride IB, T *C, vDSP_Length N) {
  T s = 0; LOOP { T d = A[n * IA] - B[n * IB]; s += d * d; } *C = s;
}
void FN(maxv)(const T *A, vDSP_Stride IA, T *C, vDSP_Length N) {
  T m = -INFTY; LOOP if (A[n * IA] > m) m = A[n * IA]; *C = m;
}
void FN(minv)(const T *A, vDSP_Stride IA, T *C, vDSP_Length N) {
  T m = INFTY; LOOP if (A[n * IA] < m) m = A[n * IA]; *C = m;
}
void FN(maxmgv)(const T *A, vDSP_Stride IA, T *C, vDSP_Length N) {
  T m = 0; LOOP if (FABS(A[n * IA]) > m) m = FABS(A[n * IA]); *C = m;
}
void FN(minmgv)(const T *A, vDSP_Stride IA, T *C, vDSP_Length N) {
  T m = INFTY; LOOP if (FABS(A[n * IA]) < m) m = FABS(A[n * IA]); *C = m;
}
// Index in Elementen des Eingabefeldes (Vielfaches des Strides), wie bei Apple
void FN(maxvi)(const T *A, vDSP_Stride IA, T *C, vDSP_Length *I, vDSP_Length N) {
  T m = -INFTY; vDSP_Length k = 0; LOOP if (A[n * IA] > m) { m = A[n * IA]; k = n; } *C = m; *I = k * IA;
}
void FN(minvi)(const T *A, vDSP_Stride IA, T *C, vDSP_Length *I, vDSP_Length N) {
  T m = INFTY; vDSP_Length k = 0; LOOP if (A[n * IA] < m) { m = A[n * IA]; k = n; } *C = m; *I = k * IA;
}
void FN(maxmgvi)(const T *A, vDSP_Stride IA, T *C, vDSP_Length *I, vDSP_Length N) {
  T m = -1; vDSP_Length k = 0; LOOP if (FABS(A[n * IA]) > m) { m = FABS(A[n * IA]); k = n; } *C = m; *I = k * IA;
}
void FN(minmgvi)(const T *A, vDSP_Stride IA, T *C, vDSP_Length *I, vDSP_Length N) {
  T m = INFTY; vDSP_Length k = 0; LOOP if (FABS(A[n * IA]) < m) { m = FABS(A[n * IA]); k = n; } *C = m; *I = k * IA;
}
// gleitendes Maximum: N Ausgaben, Fenster WindowLength (Eingabe hat N + WindowLength - 1 Werte)
void FN(vswmax)(const T *A, vDSP_Stride IA, T *C, vDSP_Stride IC, vDSP_Length N, vDSP_Length W) {
  LOOP { T m = A[n * IA]; for (vDSP_Length k = 1; k < W; k++) if (A[(n + k) * IA] > m) m = A[(n + k) * IA]; C[n * IC] = m; }
}
void FN(normalize)(const T *A, vDSP_Stride IA, T *C, vDSP_Stride IC, T *Mean, T *Std, vDSP_Length N) {
  T s = 0, q = 0; LOOP s += A[n * IA];
  T m = N ? s / (T)N : 0;
  LOOP { T d = A[n * IA] - m; q += d * d; }
  T sd = N ? SQRT(q / (T)N) : 0;
  if (C) { if (sd == 0) { LOOP C[n * IC] = 0; } else LOOP C[n * IC] = (A[n * IA] - m) / sd; }
  *Mean = m; *Std = sd;
}

// Matrizen (zeilenweise)
void FN(mmul)(const T *A, vDSP_Stride IA, const T *B, vDSP_Stride IB, T *C, vDSP_Stride IC, vDSP_Length M, vDSP_Length N, vDSP_Length P) {
  // C (M x P) = A (M x N) * B (N x P)
  for (vDSP_Length m = 0; m < M; m++)
    for (vDSP_Length p = 0; p < P; p++) {
      T s = 0;
      for (vDSP_Length k = 0; k < N; k++) s += A[(m * N + k) * IA] * B[(k * P + p) * IB];
      C[(m * P + p) * IC] = s;
    }
}
void FN(mtrans)(const T *A, vDSP_Stride IA, T *C, vDSP_Stride IC, vDSP_Length M, vDSP_Length N) {
  // A ist N x M, C ist M x N
  for (vDSP_Length m = 0; m < M; m++) for (vDSP_Length n = 0; n < N; n++) C[(m * N + n) * IC] = A[(n * M + m) * IA];
}
void FN(mmov)(const T *A, T *C, vDSP_Length M, vDSP_Length N, vDSP_Length TA, vDSP_Length TC) {
  // M x N, Spaltenweise abgelegt: TA/TC = Länge einer Spalte im Quell-/Zielfeld
  for (vDSP_Length n = 0; n < N; n++) for (vDSP_Length m = 0; m < M; m++) C[n * TC + m] = A[n * TA + m];
}

// Faltung bzw. Korrelation: C[n] = sum_p A[(n+p)*IA] * F[p*IF]; bei negativem IF (Faltung) wird F rückwärts gelesen
void FN(conv)(const T *A, vDSP_Stride IA, const T *F, vDSP_Stride IF, T *C, vDSP_Stride IC, vDSP_Length N, vDSP_Length P) {
  vDSP_Stride fs = IF;
  const T *Fb = F;   // bei negativem IF zeigt F auf das letzte Filterelement
  if (IA == 1 && IC == 1 && (fs == 1 || fs == -1)) {
    for (vDSP_Length n = 0; n < N; n++) {
      T s = 0;
      if (fs == 1) { for (vDSP_Length p = 0; p < P; p++) s += A[n + p] * Fb[p]; }
      else { for (vDSP_Length p = 0; p < P; p++) s += A[n + p] * Fb[-(vDSP_Stride)p]; }
      C[n] = s;
    }
    return;
  }
  LOOP { T s = 0; for (vDSP_Length p = 0; p < P; p++) s += A[(n + p) * IA] * Fb[(vDSP_Stride)p * fs]; C[n * IC] = s; }
}
