// Accelerate.framework für ClokeOS: vImage, nur die von Logic Pro importierten Funktionen, ohne
// CoreGraphics-Teil (vImageBuffer_InitWithCGImage und vImageCreateCGImageFromBuffer fehlen).
#include <Accelerate/Accelerate.h>
#include <stdlib.h>
#include <string.h>

vImage_Error vImageBuffer_Init(vImage_Buffer *buf, vImagePixelCount h, vImagePixelCount w, uint32_t bitsPerPixel, vImage_Flags flags) {
  size_t rb = (w * bitsPerPixel + 7) / 8;
  rb = (rb + 63) & ~(size_t)63;
  if (rb == 0) rb = 64;
  void *p = NULL;
  if (flags & kvImageNoAllocate) { buf->data = NULL; buf->height = h; buf->width = w; buf->rowBytes = rb; return (vImage_Error)(rb * (h ? h : 1)); }
  if (posix_memalign(&p, 64, rb * (h ? h : 1)) != 0) return kvImageMemoryAllocationError;
  buf->data = p; buf->height = h; buf->width = w; buf->rowBytes = rb;
  return kvImageNoError;
}

vImage_Error vImageHistogramCalculation_ARGB8888(const vImage_Buffer *src, vImagePixelCount *histogram[4], vImage_Flags flags) {
  for (int c = 0; c < 4; c++) memset(histogram[c], 0, 256 * sizeof(vImagePixelCount));
  for (vImagePixelCount y = 0; y < src->height; y++) {
    const uint8_t *r = (const uint8_t *)src->data + y * src->rowBytes;
    for (vImagePixelCount x = 0; x < src->width; x++, r += 4) for (int c = 0; c < 4; c++) histogram[c][r[c]]++;
  }
  return kvImageNoError;
}

vImage_Error vImageVerticalReflect_ARGB8888(const vImage_Buffer *src, const vImage_Buffer *dest, vImage_Flags flags) {
  if (dest->height != src->height || dest->width != src->width) return kvImageBufferSizeMismatch;
  size_t rb = src->width * 4;
  uint8_t *tmp = malloc(rb ? rb : 1);
  if (!tmp) return kvImageMemoryAllocationError;
  for (vImagePixelCount y = 0; y < (src->height + 1) / 2; y++) {
    const uint8_t *a = (const uint8_t *)src->data + y * src->rowBytes, *b = (const uint8_t *)src->data + (src->height - 1 - y) * src->rowBytes;
    uint8_t *da = (uint8_t *)dest->data + y * dest->rowBytes, *db = (uint8_t *)dest->data + (src->height - 1 - y) * dest->rowBytes;
    memcpy(tmp, a, rb); memcpy(da, b, rb); memcpy(db, tmp, rb);
  }
  free(tmp);
  return kvImageNoError;
}

// RGBA/BGRA 8 Bit: Farbe = Farbe * 255 / alpha (gerundet, auf 255 begrenzt); alpha 0 -> Farbe 0
vImage_Error vImageUnpremultiplyData_RGBA8888(const vImage_Buffer *src, const vImage_Buffer *dest, vImage_Flags flags) {
  if (dest->height != src->height || dest->width != src->width) return kvImageBufferSizeMismatch;
  for (vImagePixelCount y = 0; y < src->height; y++) {
    const uint8_t *s = (const uint8_t *)src->data + y * src->rowBytes;
    uint8_t *d = (uint8_t *)dest->data + y * dest->rowBytes;
    for (vImagePixelCount x = 0; x < src->width; x++, s += 4, d += 4) {
      unsigned a = s[3];
      for (int c = 0; c < 3; c++) {
        unsigned v = a ? (s[c] * 255u + a / 2) / a : 0;
        d[c] = v > 255 ? 255 : (uint8_t)v;
      }
      d[3] = (uint8_t)a;
    }
  }
  return kvImageNoError;
}

// Zeltfilter (getrennt): Gewichte 1,2,..,k,..,2,1 mit k = (Breite+1)/2, Summe k*k; Rand: Kante fortsetzen
// (kvImageEdgeExtend) sonst Hintergrundfarbe
vImage_Error vImageTentConvolve_ARGB8888(const vImage_Buffer *src, const vImage_Buffer *dest, void *tempBuffer, vImagePixelCount offX,
                                         vImagePixelCount offY, uint32_t kh, uint32_t kw, const Pixel_8888 bg, vImage_Flags flags) {
  if ((kh & 1) == 0 || (kw & 1) == 0) return kvImageInvalidKernelSize;
  if (flags & kvImageGetTempBufferSize) return 0;
  long H = (long)src->height, W = (long)src->width;
  long dh = (long)dest->height, dw = (long)dest->width;
  float *tmp = malloc(sizeof(float) * 4 * (size_t)(W * (dh ? dh : 1) + 1));   // nach vertikalem Schritt
  if (!tmp) return kvImageMemoryAllocationError;
  int edge = (flags & kvImageEdgeExtend) != 0;
  #define PIX(yy, xx, c) ((const uint8_t *)src->data)[(size_t)(yy) * src->rowBytes + (size_t)(xx) * 4 + (c)]
  long rh = (long)kh / 2, rw = (long)kw / 2;
  float nh = (float)(((kh + 1) / 2) * ((kh + 1) / 2)), nw = (float)(((kw + 1) / 2) * ((kw + 1) / 2));
  // vertikal (alle Quellspalten; Zeile y der Ausgabe liegt bei Quellzeile y + offY)
  for (long y = 0; y < dh; y++) for (long x = 0; x < W; x++) for (int c = 0; c < 4; c++) {
    float s = 0;
    for (long k = -rh; k <= rh; k++) {
      long yy = y + (long)offY + k; float w = (float)(rh + 1 - (k < 0 ? -k : k)), v;
      if (yy < 0 || yy >= H) { if (edge) { yy = yy < 0 ? 0 : H - 1; v = PIX(yy, x, c); } else v = bg[c]; } else v = PIX(yy, x, c);
      s += w * v;
    }
    tmp[((size_t)y * W + x) * 4 + c] = s / nh;
  }
  for (long y = 0; y < dh; y++) for (long x = 0; x < dw; x++) for (int c = 0; c < 4; c++) {
    float s = 0;
    for (long k = -rw; k <= rw; k++) {
      long xx = x + (long)offX + k; float w = (float)(rw + 1 - (k < 0 ? -k : k)), v;
      if (xx < 0 || xx >= W) { if (edge) { xx = xx < 0 ? 0 : W - 1; v = tmp[((size_t)y * W + xx) * 4 + c]; } else v = bg[c]; }
      else v = tmp[((size_t)y * W + xx) * 4 + c];
      s += w * v;
    }
    float r = s / nw + 0.5f;
    ((uint8_t *)dest->data)[(size_t)y * dest->rowBytes + (size_t)x * 4 + c] = r < 0 ? 0 : (r > 255 ? 255 : (uint8_t)r);
  }
  free(tmp);
  return kvImageNoError;
}
