/* Ersatz für <pthread/tsd_private.h>: feste TSD-Plätze für Apples Frameworks (x86_64, %gs). */
#ifndef CLOKE_PTHREAD_TSD_PRIVATE_H
#define CLOKE_PTHREAD_TSD_PRIVATE_H
#include <pthread/private.h>
#define __PTK_FRAMEWORK_COREFOUNDATION_KEY0 40
#define __PTK_FRAMEWORK_COREFOUNDATION_KEY1 41
#define __PTK_FRAMEWORK_COREFOUNDATION_KEY2 42
#define __PTK_FRAMEWORK_COREFOUNDATION_KEY3 43
#define __PTK_FRAMEWORK_COREFOUNDATION_KEY4 44
#define __PTK_FRAMEWORK_COREFOUNDATION_KEY5 45
#define __PTK_FRAMEWORK_COREFOUNDATION_KEY6 46
#define __PTK_FRAMEWORK_COREFOUNDATION_KEY7 47
#define __PTK_FRAMEWORK_COREFOUNDATION_KEY8 48
#define __PTK_FRAMEWORK_COREFOUNDATION_KEY9 49
#define __PTK_FRAMEWORK_FOUNDATION_KEY0 50
#define __PTK_FRAMEWORK_FOUNDATION_KEY1 51
#define __PTK_FRAMEWORK_FOUNDATION_KEY2 52
#define __PTK_FRAMEWORK_FOUNDATION_KEY3 53
#define __PTK_FRAMEWORK_FOUNDATION_KEY4 54
#define __PTK_FRAMEWORK_FOUNDATION_KEY5 55
#define __PTK_FRAMEWORK_FOUNDATION_KEY6 56
#define __PTK_FRAMEWORK_FOUNDATION_KEY7 57
#define __PTK_FRAMEWORK_FOUNDATION_KEY8 58
#define __PTK_FRAMEWORK_FOUNDATION_KEY9 59

static inline void *_pthread_getspecific_direct(unsigned long slot) {
    void *ret;
    __asm__("mov %%gs:%1, %0" : "=r"(ret) : "m"(*(void **)(slot * sizeof(void *))));
    return ret;
}
static inline int _pthread_setspecific_direct(unsigned long slot, void *val) {
    __asm__("movq %1, %%gs:%0" : "=m"(*(void **)(slot * sizeof(void *))) : "rn"(val));
    return 0;
}
#endif
