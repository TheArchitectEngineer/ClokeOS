# arm64: Darlings Rechnung nach der x86_64-Konvention gilt dort nicht (__LP64__ ist auch auf arm64
# gesetzt). Nach dem Einlesen aller Typen setzt cfarm64.m Offsets und Rahmenlänge nach Apples
# arm64-Konvention; _stret bleibt NO (große Rückgabewerte laufen über x8, das cfarm64.m selbst setzt).
s/^#ifdef __LP64__$/#if defined(__LP64__) \&\& !defined(__arm64__)/
s/^#if __LP64__$/#if defined(__LP64__) \&\& !defined(__arm64__)/
s/^#elif __LP64__$/#elif defined(__LP64__) \&\& !defined(__arm64__)/
s|^    // Check whether the method is oneway by reading all the$|#if defined(__arm64__)\
    {\
        extern void __CFArm64LayoutSignature(NSMethodType *, NSUInteger, NSUInteger *);\
        __CFArm64LayoutSignature(_types, _count, \&_frameLength);\
        _stret = NO;\
    }\
#endif\
\
&|
