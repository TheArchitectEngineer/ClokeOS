/* Platzhalter für OpenGL, IOKit und LaunchServices in Darlings Grafik-Frameworks.
 *
 * CoreGraphics (CGLPixelSurface, CGDirectDisplay), QuartzCore (CARenderer, CALayerContext,
 * CAOpenGLLayer) und AppKit (NSOpenGL*, X11Window) rufen gl*-, CGL*- und IOKit-Funktionen auf.
 * Gegen die echten Bibliotheken wird hier bewusst nicht gelinkt:
 *
 *   - PureDarwins OpenGL.framework re-exportiert Mesas libGL, und die lädt libgallium mit LLVM.
 *     Das bei jedem AppKit-Start zu laden kostet unter TCG viel Zeit, gebraucht wird es nur für
 *     OpenGL-Views und Layer. Darlings eigene CGL-Erweiterungen (CGLGetWindow …) hat es nicht.
 *   - IOKit gibt es in PureDarwin nur als libIOKitCF, und die lädt PureDarwins CoreFoundation.
 *     Zwei CFs in einem Prozess dürfen nicht sein.
 *
 * Deshalb bekommt jedes Framework diese Funktionen als eigene, nicht exportierte Kopie
 * (appkit/sysstubs.unexp). CGL meldet immer einen Fehler, es gibt also nie einen GL-Kontext;
 * die gl*-Funktionen tun nichts. AppKit-Fenster zeichnen ohne OpenGL (appkit/x11present.m).
 * Programme, die selbst OpenGL laden, bekommen PureDarwins echtes OpenGL.framework. */
#include <OpenGL/OpenGL.h>
#include <OpenGL/CGLInternal.h>
#include <IOKit/IOKitLib.h>
#include <IOKit/graphics/IOGraphicsLib.h>
#include <string.h>

/* --- CGL: kein Kontext, nie ------------------------------------------------------------------ */
CGLError CGLChoosePixelFormat(const CGLPixelFormatAttribute *attribs, CGLPixelFormatObj *pix, GLint *npix) {
    if (pix != NULL) *pix = NULL;
    if (npix != NULL) *npix = 0;
    return kCGLBadDisplay;
}
CGLError CGLDescribePixelFormat(CGLPixelFormatObj pix, GLint pix_num, CGLPixelFormatAttribute attrib, GLint *value) {
    if (value != NULL) *value = 0;
    return kCGLBadPixelFormat;
}
void CGLReleasePixelFormat(CGLPixelFormatObj pix) {}
CGLError CGLCreateContext(CGLPixelFormatObj pix, CGLContextObj share, CGLContextObj *ctx) {
    if (ctx != NULL) *ctx = NULL;
    return kCGLBadDisplay;
}
CGLContextObj CGLRetainContext(CGLContextObj ctx) { return ctx; }
void CGLReleaseContext(CGLContextObj ctx) {}
CGLContextObj CGLGetCurrentContext(void) { return NULL; }
CGLError CGLSetCurrentContext(CGLContextObj ctx) { return ctx == NULL ? kCGLNoError : kCGLBadContext; }
CGLError CGLFlushDrawable(CGLContextObj ctx) { return kCGLBadContext; }
CGLError CGLLockContext(CGLContextObj ctx) { return kCGLNoError; }
CGLError CGLUnlockContext(CGLContextObj ctx) { return kCGLNoError; }
CGLError CGLSetParameter(CGLContextObj ctx, CGLContextParameter pname, const GLint *params) { return kCGLBadContext; }
CGLError CGLGetParameter(CGLContextObj ctx, CGLContextParameter pname, GLint *params) { return kCGLBadContext; }
/* Darlings Erweiterungen (OpenGL/CGLInternal.h) */
CGLWindowRef CGLGetWindow(void *nativeWindow) { return NULL; }
void CGLDestroyWindow(CGLWindowRef window) {}
CGLError CGLContextMakeCurrentAndAttachToWindow(CGLContextObj context, CGLWindowRef window) { return kCGLBadContext; }
void CGLRegisterNativeDisplay(void *nativeDisplay) {}
void CGLUnmapBuffer(GLenum target) {}

/* --- gl*: ohne Kontext ohne Wirkung ---------------------------------------------------------- */
void glAlphaFunc(GLenum func, GLclampf ref) {}
void glBindBuffer(GLenum target, GLuint buffer) {}
void glBindTexture(GLenum target, GLuint texture) {}
void glBlendFunc(GLenum sfactor, GLenum dfactor) {}
void glBufferData(GLenum target, GLsizeiptr size, const GLvoid *data, GLenum usage) {}
void glClear(GLbitfield mask) {}
void glClearColor(GLclampf red, GLclampf green, GLclampf blue, GLclampf alpha) {}
void glColor4f(GLfloat red, GLfloat green, GLfloat blue, GLfloat alpha) {}
void glDeleteBuffers(GLsizei n, const GLuint *buffers) {}
void glDeleteTextures(GLsizei n, const GLuint *textures) {}
void glDepthFunc(GLenum func) {}
void glDrawArrays(GLenum mode, GLint first, GLsizei count) {}
void glEnable(GLenum cap) {}
void glEnableClientState(GLenum array) {}
void glFlush(void) {}
void glGenTextures(GLsizei n, GLuint *textures) {
    if (textures != NULL && n > 0) memset(textures, 0, sizeof(GLuint) * n);
}
GLenum glGetError(void) { return GL_NO_ERROR; }
GLboolean glIsTexture(GLuint texture) { return GL_FALSE; }
void glLoadIdentity(void) {}
GLvoid *glMapBuffer(GLenum target, GLenum access) { return NULL; }
void glMatrixMode(GLenum mode) {}
void glOrtho(GLdouble left, GLdouble right, GLdouble bottom, GLdouble top, GLdouble zNear, GLdouble zFar) {}
void glPopMatrix(void) {}
void glPushMatrix(void) {}
void glReadPixels(GLint x, GLint y, GLsizei width, GLsizei height, GLenum format, GLenum type, GLvoid *pixels) {}
void glShadeModel(GLenum mode) {}
void glTexCoordPointer(GLint size, GLenum type, GLsizei stride, const GLvoid *pointer) {}
void glTexEnvf(GLenum target, GLenum pname, GLfloat param) {}
void glTexImage2D(GLenum target, GLint level, GLint internalformat, GLsizei width, GLsizei height, GLint border,
                  GLenum format, GLenum type, const GLvoid *pixels) {}
void glTexParameteri(GLenum target, GLenum pname, GLint param) {}
void glTranslatef(GLfloat x, GLfloat y, GLfloat z) {}
GLboolean glUnmapBuffer(GLenum target) { return GL_FALSE; }
void glVertexPointer(GLint size, GLenum type, GLsizei stride, const GLvoid *pointer) {}
void glViewport(GLint x, GLint y, GLsizei width, GLsizei height) {}

/* --- IOKit: kein Zugriff auf die IORegistry (CGDisplayIOServicePort) ------------------------ */
const mach_port_t kIOMasterPortDefault = MACH_PORT_NULL;
CFMutableDictionaryRef IOServiceMatching(const char *name) { return NULL; }
kern_return_t IOServiceGetMatchingServices(mach_port_t mainPort, CFDictionaryRef matching, io_iterator_t *existing) {
    if (matching != NULL) CFRelease(matching); // wie IOKit: matching wird immer verbraucht
    if (existing != NULL) *existing = IO_OBJECT_NULL;
    return kIOReturnUnsupported;
}
io_object_t IOIteratorNext(io_iterator_t iterator) { return IO_OBJECT_NULL; }
kern_return_t IOObjectRelease(io_object_t object) { return KERN_SUCCESS; }
CFDictionaryRef IODisplayCreateInfoDictionary(io_service_t framebuffer, IOOptionBits options) { return NULL; }

/* --- LaunchServices (MacWorkspace in AppKit) ------------------------------------------------- */
/* Darlings NSWorkspace sucht und öffnet Programme über LaunchServices aus CoreServices. PureDarwins
 * CoreServices hängt an PureDarwins CoreFoundation. Bis NSWorkspace an CloakServers Programmliste
 * (apps.c) angebunden ist, findet es nichts. */
#include <LaunchServices/LaunchServices.h>
OSStatus LSFindApplicationForInfo(OSType inCreator, CFStringRef inBundleID, CFStringRef inName, FSRef *outAppRef,
                                  CFURLRef *outAppURL) {
    if (outAppURL != NULL) *outAppURL = NULL;
    return kLSApplicationNotFoundErr;
}
OSStatus LSOpenFromURLSpec(const LSLaunchURLSpec *inLaunchSpec, CFURLRef *outLaunchedURL) {
    if (outLaunchedURL != NULL) *outLaunchedURL = NULL;
    return kLSApplicationNotFoundErr;
}
