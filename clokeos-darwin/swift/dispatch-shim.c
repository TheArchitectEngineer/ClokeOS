// /usr/lib/system/libdispatch.dylib für libswift_Concurrency: PureDarwins libdispatch steckt in der libSystem, es fehlt aber
// die Datei, die Swift mit festem Pfad lädt, und zwei Voucher-Funktionen (Mach-Voucher gibt es hier nicht: NULL = kein Voucher).
// Gebaut mit -reexport_library libSystem.B.dylib, damit alle dispatch_*-Symbole dort aufgelöst werden.
void *voucher_copy(void) { return 0; }
void *voucher_adopt(void *v) { (void)v; return 0; }
