/*
 * Host-Test für ClokeTopo.h mit der echten MADT (z. B. APIC.aml vom HP):
 *   cc -o /tmp/topo_test topo_test.c && /tmp/topo_test APIC.aml [Kerne Threads smt_bits cpuid_kerne cpuid_logisch llc]
 * Standard: 10 Kerne, 12 Threads, 1 SMT-Bit, CPUID-Kerne/Paket 16, logisch/Paket 32, LLC-Teiler 32
 * (Annahmen für den Core i5-1235U; die echten Werte malt die Spur auf den Bildschirm).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ClokeTopo.h"

int main(int argc, char **argv) {
  if (argc < 2) return 2;
  FILE *f = fopen(argv[1], "rb");
  if (!f) return 2;
  static uint8_t t[8192];
  size_t len = fread(t, 1, sizeof t, f);
  fclose(f);
  unsigned cores = argc > 2 ? atoi(argv[2]) : 10, threads = argc > 3 ? atoi(argv[3]) : 12;
  unsigned smt = argc > 4 ? atoi(argv[4]) : 1;
  unsigned cp = argc > 5 ? atoi(argv[5]) : 16, lp = argc > 6 ? atoi(argv[6]) : 32, llc = argc > 7 ? atoi(argv[7]) : 32;
  uint32_t ids[64];
  unsigned n = 0;
  for (size_t off = 44; off + 2 <= len && t[off + 1];) {
    uint8_t type = t[off], l = t[off + 1];
    uint32_t id = 0, flags = 0;
    int cpu = 0;
    if (type == 0 && l >= 8) { id = t[off + 3]; flags = t[off + 4]; cpu = 1; }
    if (type == 9 && l >= 16) { memcpy(&id, t + off + 4, 4); memcpy(&flags, t + off + 8, 4); cpu = 1; }
    if (cpu && (flags & 1)) {
      unsigned d = 0;
      for (unsigned i = 0; i < n; i++) d |= ids[i] == id;
      if (!d && n < 64) ids[n++] = id;
    }
    off += l;
  }
  printf("MADT: %u aktivierte Prozessoren:", n);
  for (unsigned i = 0; i < n; i++) printf(" %u", ids[i]);
  uint32_t ids0[64];
  memcpy(ids0, ids, sizeof ids);
  unsigned m = cloke_topo_filter(ids, n, smt, cores, threads);
  printf("\nXNU erwartet %u Kerne / %u Threads, SMT-Bits %u -> %u angemeldet:", cores, threads, smt, m);
  for (unsigned i = 0; i < m; i++) printf(" %u", ids[i]);
  printf("\n");
  if (m) {
    printf("Vorhersage x86_validate_topology (cp %u lp %u llc %u): %d\n", cp, lp, llc,
           cloke_topo_predict(ids, m, cores, threads, cp, lp, llc));
    cloke_topo_params_t s;
    int r = cloke_topo_synth(&s, ids, m, smt);
    {
      cloke_topo_params_t q;
      unsigned d = 0;
      int rr = cloke_topo_synth_pairs(&q, ids0, n, &d);
      printf("alle %u CPUs als Gruppen: %d (d=%u), Pruefung %d, Threads/Kern %u\n", n, rr, d,
             rr ? -1 : cloke_topo_check(ids0, n, &q), q.nLThreadsPerCore);
    }
    printf("synthetische Parameter: %d, Pruefung: %d (Threads/Kern %u, Kerne/Die %u, Threads/Paket phys %u)\n", r,
           r ? -1 : cloke_topo_check(ids, m, &s), s.nLThreadsPerCore, s.nLCoresPerDie, s.nPThreadsPerPackage);
  }
  return m ? 0 : 1;
}
