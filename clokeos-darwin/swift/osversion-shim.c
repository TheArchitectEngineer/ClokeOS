// Ersatz für die OS-Versionsabfrage: PureDarwins dyld/libSystem meldet macOS 11.x, und os_system_version_get_current_version
// (libSystem ab macOS 10.15) fehlt. Die Swift-Runtime prüft damit #available(StdlibDeploymentTarget 6.3, *) und bricht in
// _Concurrency mit "this should never happen" ab. Dieses Shim meldet 15.6.0 (CLOKEOS_OSVERSION=major.minor.patch ändert es).
// _availability_version_check wird per __interpose ersetzt (libswiftCore und libswift_Concurrency haben je eine eigene compiler-rt-Kopie).
// Einsatz: DYLD_INSERT_LIBRARIES=./libosver.dylib. Richtig wäre ein Eingriff in dyld/libSystem des Images.
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
struct os_system_version_s { unsigned major, minor, patch; };
static void ver(unsigned *v) {
  v[0] = 15; v[1] = 6; v[2] = 0;
  const char *e = getenv("CLOKEOS_OSVERSION");
  if (e) sscanf(e, "%u.%u.%u", v, v + 1, v + 2);
}
int os_system_version_get_current_version(struct os_system_version_s *s) { unsigned v[3]; ver(v); s->major = v[0]; s->minor = v[1]; s->patch = v[2]; return 0; }
typedef struct { uint32_t platform, version; } dyld_build_version_t;
extern int _availability_version_check(uint32_t count, dyld_build_version_t versions[]);
static int my_check(uint32_t count, dyld_build_version_t versions[]) {
  unsigned v[3]; ver(v); uint32_t cur = v[0] << 16 | v[1] << 8 | v[2];
  if (getenv("OSV_DEBUG")) fprintf(stderr, "avc %u plat %u ver %x\n", count, versions[0].platform, versions[0].version);
  for (uint32_t i = 0; i < count; i++)
    if (versions[i].platform == 1 && versions[i].version > cur) return 0;   // 1 = macOS; nur diese Plattform wird verglichen
  return 1;
}
__attribute__((used, section("__DATA,__interpose"))) static struct { void *n, *o; } interp[] = { { (void *)my_check, (void *)_availability_version_check } };
