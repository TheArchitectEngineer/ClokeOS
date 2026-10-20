/* ClokeOS: Ersatz für Darlings privaten Header OpenGL/CGLInternal.h (aus darling/src/frameworks/OpenGL,
 * nicht geladen). Darling verbindet damit CGL-Kontexte mit nativen X11-Fenstern. Auf ClokeOS zeichnet
 * AppKit ohne OpenGL (siehe macapps/appkit/x11present.m); die Funktionen liefert der Platzhalter
 * OpenGL.framework aus macapps/appkit/glstub.c, der nie einen Kontext anlegt. */
#ifndef CLOKE_CGLINTERNAL_H
#define CLOKE_CGLINTERNAL_H
#include <OpenGL/OpenGL.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct _CGLWindow *CGLWindowRef;
CGLWindowRef CGLGetWindow(void *nativeWindow);
void CGLDestroyWindow(CGLWindowRef window);
CGLError CGLContextMakeCurrentAndAttachToWindow(CGLContextObj context, CGLWindowRef window);
void CGLRegisterNativeDisplay(void *nativeDisplay);
void CGLUnmapBuffer(GLenum target);
#ifdef __cplusplus
}
#endif
#endif
