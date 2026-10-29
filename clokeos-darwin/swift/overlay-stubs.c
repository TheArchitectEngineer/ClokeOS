// Ersatz für die Autolink-Marker der Apple-Overlays (libswiftObjectiveC, lib_Builtin_float): nur der Symbolname zählt.
char _swift_FORCE_LOAD_swiftObjectiveC __asm__("__swift_FORCE_LOAD_$_swiftObjectiveC");
char _swift_FORCE_LOAD_swift_Builtin_float __asm__("__swift_FORCE_LOAD_$_swift_Builtin_float");
// Rückportierungs-Archive der Toolchain (libswiftCompatibility*.a) gibt es nur für arm64: für x86_64 die Marker ersetzen;
// ohne async/Pakete wird aus den Archiven nichts gebraucht.
char _swift_FORCE_LOAD_swiftCompatibility56 __asm__("__swift_FORCE_LOAD_$_swiftCompatibility56");
char _swift_FORCE_LOAD_swiftCompatibilityConcurrency __asm__("__swift_FORCE_LOAD_$_swiftCompatibilityConcurrency");
char _swift_FORCE_LOAD_swiftCompatibilityPacks __asm__("__swift_FORCE_LOAD_$_swiftCompatibilityPacks");
char _swift_FORCE_LOAD_swiftCompatibilityDynamicReplacements __asm__("__swift_FORCE_LOAD_$_swiftCompatibilityDynamicReplacements");
