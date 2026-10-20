/* Ersatz für <Block_private.h> (Layout der Blocks-ABI, wie libclosure). */
#ifndef CLOKE_BLOCK_PRIVATE_H
#define CLOKE_BLOCK_PRIVATE_H
#include <Block.h>
#include <stdbool.h>
#include <stdint.h>

enum {
    BLOCK_DEALLOCATING      = (0x0001),
    BLOCK_REFCOUNT_MASK     = (0xfffe),
    BLOCK_NEEDS_FREE        = (1 << 24),
    BLOCK_HAS_COPY_DISPOSE  = (1 << 25),
    BLOCK_HAS_CTOR          = (1 << 26),
    BLOCK_IS_GC             = (1 << 27),
    BLOCK_IS_GLOBAL         = (1 << 28),
    BLOCK_USE_STRET         = (1 << 29),
    BLOCK_HAS_SIGNATURE     = (1 << 30),
    BLOCK_HAS_EXTENDED_LAYOUT = (1 << 31)
};
typedef void (*BlockInvokeFunction)(void *, ...);
struct Block_descriptor_1 { uintptr_t reserved; uintptr_t size; };
struct Block_descriptor_2 { void (*copy)(void *dst, const void *src); void (*dispose)(const void *); };
struct Block_descriptor_3 { const char *signature; const char *layout; };
struct Block_layout {
    void *isa;
    volatile int32_t flags;
    int32_t reserved;
    BlockInvokeFunction invoke;
    struct Block_descriptor_1 *descriptor;
};
__BEGIN_DECLS
bool _Block_has_signature(void *aBlock);
bool _Block_use_stret(void *aBlock);
const char *_Block_signature(void *aBlock);
extern void *_NSConcreteMallocBlock[32];
extern void *_NSConcreteStackBlock[32];
extern void *_NSConcreteGlobalBlock[32];
__END_DECLS
#endif
