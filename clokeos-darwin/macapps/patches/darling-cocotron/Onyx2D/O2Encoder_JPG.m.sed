# Unter DARLING gibt der JPEG-Encoder den Puffer über Darlings ELF-Brücke (elfcalls.h) mit dem
# free() der Linux-libjpeg frei. Auf ClokeOS ist libjpeg eine normale Mach-O-Bibliothek: free().
s/^#include <elfcalls.h>$/    free(outbuffer);\
#if 0/
s/^    elf_free\(outbuffer\);$/    elf_free(outbuffer);\
#endif/
