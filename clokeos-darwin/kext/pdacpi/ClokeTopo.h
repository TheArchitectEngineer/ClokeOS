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

/*
 * Rechnet x86_validate_topology() (xnu cpu_threads.c) nach: gleiche Formeln für topoParms, Zuordnung der
 * Prozessoren zu Kern, Die und Paket über die APIC-ID wie x86_lcpu_init. Ergebnis 0 = XNU wird zufrieden
 * sein; sonst Nummer der Prüfung, die dort in panic() endet (die der Bildschirm dann nicht mehr zeigt).
 * cp = cpuid_cores_per_package, lp = cpuid_logical_per_package, llc = Zahl der IDs, die sich den
 * letzten Cache teilen (cache_sharing der tiefsten Ebene).
 */
static inline int cloke_topo_predict(const uint32_t *ids, unsigned n, unsigned cores, unsigned threads,
                                     unsigned cp, unsigned lp, unsigned llc) {
  if (!cores || !threads || !cp || !lp || !llc) return 1;
  unsigned ltpc = threads / cores;
  if (!ltpc) return 2;
  unsigned ptpc = lp / cp;
  if (!ptpc) return 3;
  unsigned sharing = llc / ltpc; /* nCoresSharingLLC */
  if (llc > cores) sharing = cores;
  if (!sharing) return 4;
  unsigned ldpp = cores / sharing;
  unsigned pcpd = llc / ptpc;
  if (!pcpd) return 5;
  unsigned pdpp = cp / pcpd;
  unsigned lcpd = sharing, lcpp = lcpd * ldpp, ltpd = ltpc * lcpd, ltpp = ltpc * lcpp;
  unsigned ptpd = ptpc * pcpd, ptpp = ptpc * pcpd * pdpp;
  if (!ptpd || !ptpp) return 6;

  /* Pakete, Dies und Kerne nach der APIC-ID gruppieren */
  unsigned npk = 0;
  uint32_t pk[64];
  for (unsigned i = 0; i < n; i++) {
    uint32_t k = ids[i] / ptpp;
    unsigned f = 0;
    for (unsigned j = 0; j < npk; j++) f |= pk[j] == k;
    if (!f && npk < 64) pk[npk++] = k;
  }
  if (npk * ltpp != n) return 7; /* "%d threads but %d registered" */
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
    if (dies != ldpp) return 8; /* "Should have %d Dies" */
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
      if (nc != lcpd) return 9;   /* "Should have %d Cores ... Die" */
      if (ncpu != ltpd) return 10; /* "Should have %d Threads ... Die" */
      cores_in_pkg += nc;
      for (unsigned m = 0; m < nc; m++) {
        unsigned t = 0;
        for (unsigned i = 0; i < n; i++)
          if (ids[i] / ptpp == pk[a] && ids[i] / ptpd == seen_die[j] && ids[i] / ptpc == seen_core[m]) t++;
        if (t != ltpc) return 11; /* "Should have %d Threads ... Core" */
      }
    }
    if (cores_in_pkg != lcpp) return 12;
    if (cpus_in_pkg != ltpp) return 13;
  }
  return 0;
}
#endif
