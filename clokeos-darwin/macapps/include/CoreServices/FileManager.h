/* Ersatz für Darlings <CoreServices/FileManager.h>: nur die FSRef-Typen. CFURL holt sich
 * FSRefMakePath/FSPathMakeRef zur Laufzeit per dlsym aus CoreServices. */
#ifndef CLOKE_CORESERVICES_FILEMANAGER_H
#define CLOKE_CORESERVICES_FILEMANAGER_H
#include <MacTypes.h>
#ifndef __FILES__
struct FSRef { UInt8 hidden[80]; };
typedef struct FSRef FSRef;
#endif
/* als Makro, damit Darlings "const FSRefPtr" zu Apples "const struct FSRef *" passt */
#undef FSRefPtr
#define FSRefPtr struct FSRef *
#endif
