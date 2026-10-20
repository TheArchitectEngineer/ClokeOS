/* ClokeOS: Apples ImageIO.h plus die Konstante kCGImageDestinationDPI, die nur Darlings ImageIO
 * kennt (NSBitmapImageRep benutzt sie). Exportiert von appkit/imageio.m. */
#include_next <ImageIO/ImageIO.h>
#ifndef CLOKE_IMAGEIO_DPI
#define CLOKE_IMAGEIO_DPI
IMAGEIO_EXTERN const CFStringRef kCGImageDestinationDPI;
#endif
