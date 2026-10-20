/* Ersatz für Apples ICU-Erweiterung <unicode/ualoc.h> */
#ifndef CLOKE_UALOC_H
#define CLOKE_UALOC_H
#include <unicode/uloc.h>
/* fehlt in PureDarwins ICU 76: Apples Variante kennt zusätzliche Eltern (z. B. en_150 -> en_GB).
 * uloc_getParent liefert die normale ICU-Kette. */
static inline int32_t ualoc_getAppleParent(const char *localeID, char *parent, int32_t parentCapacity, UErrorCode *err) {
    return uloc_getParent(localeID, parent, parentCapacity, err);
}
#endif
