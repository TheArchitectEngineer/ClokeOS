/* Wird beim Bau von CoreFoundation per -include geladen: Apples ICU-Erweiterungen, die
 * PureDarwins ICU 76 nicht hat (fehlt in PureDarwin). */
#ifndef CLOKE_CF_COMPAT_H
#define CLOKE_CF_COMPAT_H
#ifndef __ASSEMBLER__
/* PureDarwins objc-internal.h erwartet Makros, die Apples neueres SDK nicht mehr hat */
#include <Availability.h>
#include <objc/objc-api.h>
#ifndef OBJC2_UNAVAILABLE
#define OBJC2_UNAVAILABLE
#endif
#ifndef __BRIDGEOS_UNAVAILABLE
#define __BRIDGEOS_UNAVAILABLE
#endif
#ifndef __BRIDGEOS_AVAILABLE
#define __BRIDGEOS_AVAILABLE(v)
#endif
/* alter objc-Typ (objc-runtime.h vor 10.5), für Darlings NSInvocation */
typedef void *marg_list;
/* objc4-Funktionen ohne Deklaration im SDK (PureDarwins libobjc exportiert sie) */
#include <objc/objc.h>
OBJC_EXPORT BOOL objc_isAuto(id object);
OBJC_EXPORT BOOL _class_isFutureClass(Class cls);
#include <unicode/udatpg.h>
#include <unicode/ustring.h>
#define UADATPG_FORCE_12_HOUR_CYCLE (1 << 16)
#define UADATPG_FORCE_24_HOUR_CYCLE (1 << 17)
/* Apple stellt damit 12/24-Stunden-Muster um; ohne Apple-ICU bleibt das Muster unverändert. */
static inline int32_t uadatpg_remapPatternWithOptions(UDateTimePatternGenerator *dtpg, const UChar *pattern,
        int32_t patternLength, int32_t options, UChar *newPattern, int32_t newPatternCapacity, UErrorCode *status) {
    (void)dtpg; (void)options;
    if (U_FAILURE(*status)) return 0;
    if (patternLength < 0) patternLength = u_strlen(pattern);
    if (patternLength > newPatternCapacity) { *status = U_BUFFER_OVERFLOW_ERROR; return patternLength; }
    u_memcpy(newPattern, pattern, patternLength);
    if (patternLength < newPatternCapacity) newPattern[patternLength] = 0;
    return patternLength;
}
#endif
#endif
