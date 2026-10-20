/* Funktionen, die Darlings CoreFoundation und Foundation aus libSystem erwarten, die PureDarwins
 * libSystem (20.5) aber nicht exportiert. CF und Foundation bekommen je eine eigene Kopie; die
 * Liste syscompat.unexp hält sie beim Linken aus den Exporten (ein visibility-Attribut geht
 * nicht, weil Apples Header sie schon als exportiert deklarieren).
 *
 * Für Mac-Programme selbst schließt das die Lücken nicht: Die binden solche Symbole an
 * libSystem. Das ist ein eigener Schritt (siehe ~/ClokeOS-work/upstream/appkit-machbarkeit.md). */
#include <dispatch/dispatch.h>
#include <errno.h>
#include <libkern/OSAtomic.h>
#include <mach/mach.h>
#include <malloc/malloc.h>
#include <servers/bootstrap.h>
#include <spawn.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <sysdir.h>
#include <unistd.h>
#include <xpc/xpc.h>
#include <bsm/audit.h>

bool OSAtomicCompareAndSwap64Barrier(int64_t oldValue, int64_t newValue, volatile int64_t *theValue) {
  return __sync_bool_compare_and_swap(theValue, oldValue, newValue);
}

/* CF braucht das nur für __CFDiscorporateMemory (Speicher, den der Kernel ohne Kopie teilt). */
kern_return_t mach_make_memory_entry_64(vm_map_t target_task, memory_object_size_t *size,
                                               memory_object_offset_t offset, vm_prot_t permission,
                                               mach_port_t *object_handle, mem_entry_name_port_t parent_entry) {
  return KERN_NOT_SUPPORTED;
}

/* Die alte API aus NSSystemDirectories.h; PureDarwin hat nur die neue sysdir-API. */
unsigned int NSStartSearchPathEnumeration(unsigned int directory, unsigned int domainMask) {
  return sysdir_start_search_path_enumeration((sysdir_search_path_directory_t)directory,
                                              (sysdir_search_path_domain_mask_t)domainMask);
}
unsigned int NSGetNextSearchPathEnumeration(unsigned int state, char *path) {
  return sysdir_get_next_search_path_enumeration(state, path);
}

/* veraltet seit 10.5, aber Foundation (NSMachBootstrapServer) ruft es noch */
extern kern_return_t bootstrap_register2(mach_port_t bp, name_t service_name, mach_port_t sp, uint64_t flags);
kern_return_t bootstrap_register(mach_port_t bp, name_t service_name, mach_port_t sp) {
  return bootstrap_register2(bp, service_name, sp, 0);
}

const char *malloc_get_zone_name(malloc_zone_t *zone) {
  return zone ? zone->zone_name : NULL;
}

/* Den Kern (posix_spawn mit chdir) kann XNU 20, nur der libSystem-Wrapper fehlt.
 * NSTask mit currentDirectoryPath schlägt deshalb mit ENOTSUP fehl. */
int posix_spawn_file_actions_addchdir_np(posix_spawn_file_actions_t *actions, const char *path) {
  return ENOTSUP;
}

void xpc_connection_get_audit_token(xpc_connection_t connection, audit_token_t *token) {
  if (token) memset(token, 0, sizeof *token);
}

/* PureDarwins libdispatch hat dispatch_data, aber kein dispatch_io und kein dispatch_read.
 * NSFileHandle benutzt sie für readabilityHandler und Lesen im Hintergrund. dispatch_read ist hier
 * ein einfacher read() auf einer Hintergrund-Queue; dispatch_io meldet ENOTSUP. */
void dispatch_read(dispatch_fd_t fd, size_t length, dispatch_queue_t queue,
                          void (^handler)(dispatch_data_t data, int error)) {
  dispatch_async(dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0), ^{
    size_t cap = length == SIZE_MAX ? 65536 : length;
    void *buf = malloc(cap ? cap : 1);
    ssize_t n = buf ? read(fd, buf, cap) : -1;
    int err = n < 0 ? errno : 0;
    dispatch_data_t data = dispatch_data_create(buf, n > 0 ? (size_t)n : 0, NULL, DISPATCH_DATA_DESTRUCTOR_DEFAULT);
    free(buf);
    dispatch_async(queue, ^{ handler(data, err); dispatch_release(data); });
  });
}
dispatch_io_t dispatch_io_create(dispatch_io_type_t type, dispatch_fd_t fd, dispatch_queue_t queue,
                                        void (^cleanup_handler)(int error)) {
  if (cleanup_handler) dispatch_async(queue, ^{ cleanup_handler(ENOTSUP); });
  return NULL;
}
void dispatch_io_read(dispatch_io_t channel, off_t offset, size_t length, dispatch_queue_t queue,
                             dispatch_io_handler_t io_handler) {
  dispatch_async(queue, ^{ io_handler(true, NULL, ENOTSUP); });
}
void dispatch_io_set_low_water(dispatch_io_t channel, size_t low_water) {}
