// PureDarwin hat kein DMA-BUF/GBM: DMABufBuffer.cpp ist dann leer, CoordinatedPlatformLayerBufferDMABuf.cpp ruft die Funktion trotzdem.
#include "config.h"
#include "DMABufBuffer.h"

#if !USE(GBM)
#include <epoxy/egl.h>
namespace WebCore {
EGLImage DMABufBuffer::createEGLImage(GLDisplay&, const Attributes&) { return nullptr; }
}
#endif
