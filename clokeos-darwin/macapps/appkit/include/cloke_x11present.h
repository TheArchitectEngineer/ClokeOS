/* ClokeOS: Bildausgabe für Darlings X11Window ohne OpenGL (macapps/appkit/x11present.m). */
#import <X11/Xlib.h>
#import <X11/Xutil.h>
@class O2Surface;

/* Visual für neue Fenster: das Standard-Visual des Bildschirms statt glXChooseVisual. */
XVisualInfo *ClokeX11VisualInfo(Display *dpy, int screen);
/* Kopiert das Surface ins Fenster (XShmPutImage, sonst XPutImage). *state gehört dem Fenster. */
void ClokeX11Present(void **state, Display *dpy, Window win, XVisualInfo *vi, O2Surface *surface);
void ClokeX11PresentFree(void **state, Display *dpy);
