# ClokeOS zeichnet AppKit-Fenster ohne GLX (siehe macapps/appkit/x11present.m).
# XVisualInfo kommt aus Xutil.h statt aus GL/glx.h; das Fenster merkt sich seinen Ausgabepuffer.
s|^#import <GL/glx.h>$|#import <X11/Xutil.h>|
s|^    O2Context \*_context;$|    O2Context *_context;\
    void *_clokePresent; /* ClokeOS: XShm-/XPutImage-Puffer */|
