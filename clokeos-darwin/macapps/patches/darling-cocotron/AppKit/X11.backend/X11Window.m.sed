# ClokeOS: Fensterinhalt per XShmPutImage/XPutImage statt über CGL/GLX (macapps/appkit/x11present.m).
#  1. Kein GLX-Visual: das Fenster bekommt das Standard-Visual des Bildschirms.
#  2. Kein CGL-Fenster.
#  3. flushBuffer kopiert das Onyx2D-Surface direkt ins X-Fenster.
#  4. Der Ausgabepuffer wird mit dem Fenster freigegeben.
s|^#import "X11Window.h"$|#import "X11Window.h"\
#import "cloke_x11present.h"|
/^    \/\/ TODO: get rid of these glX calls$/,/^                   None};$/d
s|_visualInfo = glXChooseVisual\(_display, screen, att\)|_visualInfo = ClokeX11VisualInfo(_display, screen)|
s|NSLog\(@"glXChooseVisual failed|NSLog(@"ClokeX11VisualInfo failed|
s|^    _cglWindow = CGLGetWindow\(\(void \*\) _window\);$|    _cglWindow = NULL;|
s|^    \[self openGLFlushBuffer\];$|    ClokeX11Present(\&_clokePresent, _display, _window, _visualInfo, [_context surface]);|
s|^        XDestroyWindow\(_display, _window\);$|        ClokeX11PresentFree(\&_clokePresent, _display);\
        XDestroyWindow(_display, _window);|
