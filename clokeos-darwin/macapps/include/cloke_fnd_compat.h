/* Wird beim Bau von Foundation per -include geladen: Header, die Darlings SDK nebenbei
 * mitbringt, private CF-Funktionen aus Darlings CoreFoundation und Ersatz für Teile von
 * libdispatch, die PureDarwin fehlen. */
#ifndef CLOKE_FND_COMPAT_H
#define CLOKE_FND_COMPAT_H
#ifndef __ASSEMBLER__
#include <mach/mach.h>
#include <mach/mach_error.h>
#include <mach/vm_param.h>
#include <CoreFoundation/CoreFoundation.h>
#include <dispatch/dispatch.h>
CF_EXPORT CFDataRef _CFPropertyListCreateXMLDataWithExtras(CFAllocatorRef allocator, CFPropertyListRef propertyList);

/* fehlt in PureDarwin: _dispatch_data_empty und _dispatch_data_destructor_free */
static inline dispatch_data_t cloke_dispatch_data_empty(void) {
  static dispatch_data_t empty;
  static dispatch_once_t once;
  dispatch_once(&once, ^{ empty = dispatch_data_create("", 0, NULL, DISPATCH_DATA_DESTRUCTOR_DEFAULT); });
  return empty;
}
#undef dispatch_data_empty
#define dispatch_data_empty cloke_dispatch_data_empty()
/* Ersatz kopiert die Daten, statt den Puffer zu übernehmen; der Puffer des Aufrufers wird nicht
 * freigegeben (kleines Leck, aber kein Absturz). */
#undef DISPATCH_DATA_DESTRUCTOR_FREE
#define DISPATCH_DATA_DESTRUCTOR_FREE DISPATCH_DATA_DESTRUCTOR_DEFAULT
#endif
#endif
