#ifndef SPLASH_H
#define SPLASH_H

/* ClokeOS boot logo: the cape from clokeos.html, white on black, drawn
 * centred through GOP before the kernel starts. */

#include "common.h"

/* Renders the logo as coverage (0..255) into alpha[size * size], size at most
 * 1024. No firmware calls, so it can be tested on the build host as well. */
VOID splash_render(UINT8 *alpha, UINT32 size);

EFI_STATUS splash_show(AppContext *ctx);

#endif
