// PureDarwin: bmalloc/ProcessCheck.mm gibt es nur für Cocoa. Kein Prozessname-Filter, Gigacage und Mini-Mode wie auf Linux.
#include "ProcessCheck.h"
#include <stdlib.h>

namespace bmalloc {

const char* processNameString()
{
    const char* name = getprogname();
    return name ? name : "";
}

bool gigacageEnabledForProcess() { return true; }

bool shouldAllowMiniMode() { return true; }

} // namespace bmalloc
