/*
 * Welche Prozessoren aus der MADT darf der Kernel bekommen?
 *
 * XNUs x86_validate_topology() rechnet aus MSR_CORE_THREAD_COUNT (0x35) und CPUID, wie viele Threads das
 * Paket hat: Threads pro Kern = thread_count / core_count (ganzzahlig), mal die Kerne. Das stimmt nur bei
 * gleichmäßigem SMT. Ein Hybrid-Prozessor (Alder Lake: 2 P-Kerne mit je 2 Threads + 8 E-Kerne mit je einem
 * = 12 Threads auf 10 Kernen) ergibt 12 / 10 = 1, also 10 Threads, und bei 12 gemeldeten Prozessoren (oder
 * bei jeder anderen Zahl wie cpus=2) bricht der Kernel mit panic ab. Die Ausgabe ist da längst abgeschaltet,
 * der Rechner steht still.
 *
 * Abhilfe: nur so viele Threads je Kern anmelden, wie XNU erwartet (bei 12/10 also einen). Reine C-Funktion
 * ohne Kernel-Abhängigkeiten, damit tools/topo_test.c sie mit der echten MADT des HP prüfen kann.
 */
#ifndef CLOKE_TOPO_H
#define CLOKE_TOPO_H
#include <stdint.h>

/*
 * ids[0] ist der Boot-Prozessor (bleibt vorn), n die Zahl der aktivierten Prozessoren der MADT,
 * smt_shift die Zahl der APIC-ID-Bits für den Thread innerhalb eines Kerns (CPUID 0xB, Ebene 0),
 * cores/threads sind core_count/thread_count aus cpuid_info() (genau das, was XNU auch sieht).
 * Ergebnis: neue Anzahl, ids ist darauf gekürzt (Reihenfolge bleibt); 0 = keine stimmige Auswahl möglich.
 */
static inline unsigned cloke_topo_filter(uint32_t *ids, unsigned n, unsigned smt_shift, unsigned cores,
                                         unsigned threads) {
  if (n == 0 || cores == 0 || threads == 0) return n; /* keine Angaben: nichts ändern */
  unsigned tpc = threads / cores;
  if (tpc == 0) tpc = 1;
  if (n == cores * tpc) return n; /* stimmt schon */
  unsigned out = 0;
  for (unsigned i = 0; i < n; i++) {
    unsigned same = 0;
    for (unsigned j = 0; j < out; j++)
      if ((ids[j] >> smt_shift) == (ids[i] >> smt_shift)) same++;
    if (same < tpc) ids[out++] = ids[i];
  }
  return out == cores * tpc ? out : 0;
}

/* Spiegel von x86_topology_parameters_t (xnu osfmk/i386/cpu_topology.h); topoParms liegt im Kernel */
typedef struct {
  uint32_t LLCDepth, nCoresSharingLLC, nLCPUsSharingLLC, maxSharingLLC, nLThreadsPerCore, nPThreadsPerCore,
      nLCoresPerDie, nPCoresPerDie, nLDiesPerPackage, nPDiesPerPackage, nLThreadsPerDie, nPThreadsPerDie,
      nLThreadsPerPackage, nPThreadsPerPackage, nLCoresPerPackage, nPCoresPerPackage, nPackages, stable;
} cloke_topo_params_t;

/*
 * Rechnet initTopoParms() (xnu cpu_threads.c) nach: aus Kernen/Threads (MSR 0x35), CPUID-Kernen und
 * logischen IDs je Paket (cp, lp) und der Zahl der IDs, die sich den letzten Cache teilen (llc).
 * LLCDepth, nPackages und stable bleiben unberührt. Ergebnis 0 = gerechnet, sonst Teiler null.
 */
static inline int cloke_topo_fill(cloke_topo_params_t *t, unsigned cores, unsigned threads, unsigned cp,
                                  unsigned lp, unsigned llc) {
  if (!cores || !threads || !cp || !lp || !llc) return 1;
  unsigned ltpc = threads / cores, ptpc = lp / cp;
  if (!ltpc || !ptpc) return 2;
  unsigned share = llc / ltpc;
  if (llc > cores) share = cores;
  if (!share || !(llc / ptpc)) return 3;
  t->maxSharingLLC = llc;
  t->nCoresSharingLLC = share;
  t->nLCPUsSharingLLC = llc > threads ? threads : llc;
  t->nLThreadsPerCore = ltpc;
  t->nPThreadsPerCore = ptpc;
  t->nLDiesPerPackage = cores / share;
  t->nPDiesPerPackage = cp / (llc / ptpc);
  t->nLCoresPerDie = share;
  t->nPCoresPerDie = llc / ptpc;
  t->nLThreadsPerDie = ltpc * t->nLCoresPerDie;
  t->nPThreadsPerDie = ptpc * t->nPCoresPerDie;
  t->nLCoresPerPackage = t->nLCoresPerDie * t->nLDiesPerPackage;
  t->nPCoresPerPackage = t->nPCoresPerDie * t->nPDiesPerPackage;
  t->nLThreadsPerPackage = ltpc * t->nLCoresPerPackage;
  t->nPThreadsPerPackage = ptpc * t->nPCoresPerPackage;
  return 0;
}

/*
 * Rechnet x86_validate_topology() nach: Prozessoren werden über die APIC-ID wie in x86_lcpu_init Kern,
 * Die und Paket zugeordnet und gegen die Parameter t geprüft. 0 = XNU wird zufrieden sein, sonst die Nummer
 * der Prüfung, die dort in panic() endet (die der Bildschirm dann nicht mehr zeigt).
 */
static inline int cloke_topo_check(const uint32_t *ids, unsigned n, const cloke_topo_params_t *t) {
  unsigned ltpc = t->nLThreadsPerCore, ptpc = t->nPThreadsPerCore, ptpd = t->nPThreadsPerDie,
           ptpp = t->nPThreadsPerPackage;
  if (!ltpc || !ptpc || !ptpd || !ptpp) return 6;
  unsigned npk = 0;
  uint32_t pk[64];
  for (unsigned i = 0; i < n; i++) {
    uint32_t k = ids[i] / ptpp;
    unsigned f = 0;
    for (unsigned j = 0; j < npk; j++) f |= pk[j] == k;
    if (!f && npk < 64) pk[npk++] = k;
  }
  if (npk * t->nLThreadsPerPackage != n) return 7; /* "%d threads but %d registered" */
  for (unsigned a = 0; a < npk; a++) {
    unsigned dies = 0, cores_in_pkg = 0, cpus_in_pkg = 0;
    uint32_t seen_die[64];
    for (unsigned i = 0; i < n; i++) {
      if (ids[i] / ptpp != pk[a]) continue;
      cpus_in_pkg++;
      uint32_t d = ids[i] / ptpd;
      unsigned f = 0;
      for (unsigned j = 0; j < dies; j++) f |= seen_die[j] == d;
      if (!f && dies < 64) seen_die[dies++] = d;
    }
    if (dies != t->nLDiesPerPackage) return 8; /* "Should have %d Dies" */
    for (unsigned j = 0; j < dies; j++) {
      unsigned nc = 0, ncpu = 0;
      uint32_t seen_core[64];
      for (unsigned i = 0; i < n; i++) {
        if (ids[i] / ptpp != pk[a] || ids[i] / ptpd != seen_die[j]) continue;
        ncpu++;
        uint32_t c = ids[i] / ptpc;
        unsigned f = 0;
        for (unsigned m = 0; m < nc; m++) f |= seen_core[m] == c;
        if (!f && nc < 64) seen_core[nc++] = c;
      }
      if (nc != t->nLCoresPerDie) return 9;    /* "Should have %d Cores ... Die" */
      if (ncpu != t->nLThreadsPerDie) return 10; /* "Should have %d Threads ... Die" */
      cores_in_pkg += nc;
      for (unsigned m = 0; m < nc; m++) {
        unsigned c = 0;
        for (unsigned i = 0; i < n; i++)
          if (ids[i] / ptpp == pk[a] && ids[i] / ptpd == seen_die[j] && ids[i] / ptpc == seen_core[m]) c++;
        if (c != ltpc) return 11; /* "Should have %d Threads ... Core" */
      }
    }
    if (cores_in_pkg != t->nLCoresPerPackage) return 12;
    if (cpus_in_pkg != t->nLThreadsPerPackage) return 13;
  }
  return 0;
}

/* Beides zusammen, wie XNU es aus den CPUID-Werten bildet (Host-Test) */
static inline int cloke_topo_predict(const uint32_t *ids, unsigned n, unsigned cores, unsigned threads,
                                     unsigned cp, unsigned lp, unsigned llc) {
  cloke_topo_params_t t = {0};
  int r = cloke_topo_fill(&t, cores, threads, cp, lp, llc);
  return r ? r : cloke_topo_check(ids, n, &t);
}

/*
 * Parameter, die zu genau diesen Prozessoren passen: ein Thread je Kern der Auswahl (oder mehr, wenn die
 * Auswahl es hat), alle in einem Die und Paket, die APIC-ID-Bits wie bei der echten CPU (smt_shift).
 * Das ist die Beschreibung, die XNU bei Hybrid-Chips aus CPUID nicht bekommt (Alder Lake: logische IDs je
 * Paket kleiner als Kerne je Paket, also null Threads je Kern in der physischen Rechnung).
 */
static inline int cloke_topo_synth(cloke_topo_params_t *t, const uint32_t *ids, unsigned n, unsigned smt_shift) {
  unsigned maxid = 0, cores = 0, tpc = 0;
  for (unsigned i = 0; i < n; i++) {
    if (ids[i] > maxid) maxid = ids[i];
    unsigned same = 0, first = 1;
    for (unsigned j = 0; j < n; j++)
      if ((ids[j] >> smt_shift) == (ids[i] >> smt_shift)) {
        same++;
        if (j < i) first = 0;
      }
    if (first) cores++;
    if (same > tpc) tpc = same;
  }
  unsigned L = 2;
  while (L <= maxid) L <<= 1;
  if (L < (2u << smt_shift)) L = 2u << smt_shift;
  return cloke_topo_fill(t, cores, cores * tpc, L >> smt_shift, L, L);
}
#endif
