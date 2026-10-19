#include "splash.h"
#include "clokeos_logo.h"
#ifndef SPLASH_HOST_TEST
#include "app.h"
#endif

/* Logo height as a share of the shorter screen side, in percent. 96 px on
 * QEMU's 1280x800, the size the boot screen in clokeos.html uses. */
#define SPLASH_SIZE_PERCENT 12
#define SPLASH_MIN_SIZE     48
#define SPLASH_MAX_SIZE     1024

/* Vertical samples per pixel row; horizontally the coverage is exact. */
#define SPLASH_SUBROWS 4
#define SPLASH_MAX_CROSSINGS 64

#define LOGO_POINTS (sizeof(clokeos_logo_pts) / sizeof(clokeos_logo_pts[0]))
#define LOGO_PATHS  (sizeof(clokeos_logo_ends) / sizeof(clokeos_logo_ends[0]))

/*
 * Scanline fill with the even-odd rule, the fill-rule the SVG path uses (the
 * hood opening, the clasp and the folds are holes in the cape). Everything is
 * fixed point: coordinates in 1/256 pixel.
 */
VOID splash_render(UINT8 *alpha, UINT32 size) {
  static INT32 px[LOGO_POINTS][2];
  static INT32 cov[SPLASH_MAX_SIZE];
  INT32 xs[SPLASH_MAX_CROSSINGS];
  INT32 limit = (INT32)size * 256;

  for (UINTN i = 0; i < LOGO_POINTS; i++) {
    px[i][0] = (INT32)((INT64)clokeos_logo_pts[i][0] * size / CLOKEOS_LOGO_UNITS);
    px[i][1] = (INT32)((INT64)clokeos_logo_pts[i][1] * size / CLOKEOS_LOGO_UNITS);
  }

  for (UINT32 row = 0; row < size; row++) {
    for (UINT32 x = 0; x < size; x++)
      cov[x] = 0;

    for (UINT32 sub = 0; sub < SPLASH_SUBROWS; sub++) {
      INT32 sy = (INT32)row * 256 + (INT32)((2 * sub + 1) * 256 / (2 * SPLASH_SUBROWS));
      UINTN n = 0, first = 0;

      for (UINTN p = 0; p < LOGO_PATHS; p++) {
        UINTN last = clokeos_logo_ends[p];
        for (UINTN i = first; i < last; i++) {
          INT32 *a = px[i];
          INT32 *b = px[i + 1 < last ? i + 1 : first];
          if (a[1] == b[1] || sy < (a[1] < b[1] ? a[1] : b[1]) ||
              sy >= (a[1] < b[1] ? b[1] : a[1]))
            continue;
          if (n < SPLASH_MAX_CROSSINGS)
            xs[n++] = a[0] + (INT32)((INT64)(sy - a[1]) * (b[0] - a[0]) / (b[1] - a[1]));
        }
        first = last;
      }

      for (UINTN i = 1; i < n; i++) {
        INT32 v = xs[i];
        UINTN j = i;
        for (; j > 0 && xs[j - 1] > v; j--)
          xs[j] = xs[j - 1];
        xs[j] = v;
      }

      for (UINTN i = 0; i + 1 < n; i += 2) {
        INT32 x0 = xs[i] < 0 ? 0 : xs[i];
        INT32 x1 = xs[i + 1] > limit ? limit : xs[i + 1];
        if (x0 >= x1)
          continue;
        INT32 p0 = x0 >> 8, p1 = x1 >> 8;
        if (p0 == p1) {
          cov[p0] += x1 - x0;
          continue;
        }
        cov[p0] += 256 - (x0 & 255);
        for (INT32 p = p0 + 1; p < p1; p++)
          cov[p] += 256;
        if (p1 < (INT32)size)
          cov[p1] += x1 & 255;
      }
    }

    for (UINT32 x = 0; x < size; x++) {
      INT32 v = cov[x] / SPLASH_SUBROWS;
      alpha[row * size + x] = (UINT8)(v > 255 ? 255 : v);
    }
  }
}

#ifndef SPLASH_HOST_TEST
EFI_STATUS splash_show(AppContext *ctx) {
  EFI_GRAPHICS_OUTPUT_PROTOCOL *gop = NULL;
  EFI_GRAPHICS_OUTPUT_BLT_PIXEL black = {0, 0, 0, 0};
  EFI_GRAPHICS_OUTPUT_BLT_PIXEL *pixels = NULL;
  UINT8 *alpha = NULL;
  EFI_STATUS status;

  status = uefi_call_wrapper(ctx->bs->LocateProtocol, 3,
      &gEfiGraphicsOutputProtocolGuid, NULL, (VOID **)&gop);
  if (EFI_ERROR(status) || !gop || !gop->Mode || !gop->Mode->Info)
    return EFI_ERROR(status) ? status : EFI_UNSUPPORTED;

  UINT32 width = gop->Mode->Info->HorizontalResolution;
  UINT32 height = gop->Mode->Info->VerticalResolution;
  UINT32 size = (width < height ? width : height) * SPLASH_SIZE_PERCENT / 100;
  if (size < SPLASH_MIN_SIZE)
    size = SPLASH_MIN_SIZE;
  if (size > SPLASH_MAX_SIZE)
    size = SPLASH_MAX_SIZE;
  if (size > width || size > height)
    return EFI_UNSUPPORTED;

  status = app_alloc_pool(ctx, (UINTN)size * size, (VOID **)&alpha);
  if (!EFI_ERROR(status))
    status = app_alloc_pool(ctx, (UINTN)size * size * sizeof(*pixels), (VOID **)&pixels);
  if (EFI_ERROR(status)) {
    app_free_pool(ctx, alpha);
    return status;
  }

  splash_render(alpha, size);
  for (UINTN i = 0; i < (UINTN)size * size; i++) {
    pixels[i].Blue = pixels[i].Green = pixels[i].Red = alpha[i];
    pixels[i].Reserved = 0;
  }

  /* The firmware's text cursor would otherwise sit on top of the logo. */
  if (ctx->st->ConOut)
    uefi_call_wrapper(ctx->st->ConOut->EnableCursor, 2, ctx->st->ConOut, FALSE);

  status = uefi_call_wrapper(gop->Blt, 10, gop, &black, EfiBltVideoFill,
      0, 0, 0, 0, width, height, 0);
  if (!EFI_ERROR(status))
    status = uefi_call_wrapper(gop->Blt, 10, gop, pixels, EfiBltBufferToVideo,
        0, 0, (width - size) / 2, (height - size) / 2, size, size, 0);

  app_free_pool(ctx, pixels);
  app_free_pool(ctx, alpha);
  return status;
}
#endif
