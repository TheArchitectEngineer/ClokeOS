/* Ersatz für <mach-o/dyld_priv.h> */
#ifndef CLOKE_DYLD_PRIV_H
#define CLOKE_DYLD_PRIV_H
#include <mach-o/dyld.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
__BEGIN_DECLS
const char *dyld_image_path_containing_address(const void *addr);
bool _dyld_is_memory_immutable(const void *addr, size_t length);
uint32_t dyld_get_program_sdk_version(void);
__END_DECLS
#endif
