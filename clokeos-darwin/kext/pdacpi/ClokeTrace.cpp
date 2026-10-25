#include <IOKit/IOLib.h>
#include <pexpert/pexpert.h>
#include <stdarg.h>
#include "ClokeTrace.h"
#include "ClokeTraceFont.h"

extern "C" vm_offset_t ml_io_map(vm_offset_t phys_addr, vm_size_t size);

enum { kLines = 22, kCols = 80, kScale = 2, kCellW = 6 * kScale, kCellH = 8 * kScale };

static char gLog[kLines][kCols + 1];
static unsigned gCount;
static bool gInit, gOK;
static volatile uint8_t *gBase;
static uint32_t gRowBytes, gWidth, gHeight, gCols;

static void trace_init(bool force) {
  gInit = true;
  uint32_t on = 0; /* Standard: aus; cloke_trace=1 malt mit, bei einem Fehler zeigt cloke_trace_show() die Zeilen */
  PE_parse_boot_argn("cloke_trace", &on, sizeof on);
  if (!on && !force) return;
  PE_Video v;
  if (PE_current_console(&v) != 0 || !v.v_baseAddr || v.v_width < 320 || v.v_height < 240) return;
  /* v_baseAddr bleibt bis zum Schluss die physische Adresse (pexpert/i386/pe_init.c); ist sie schon
   * umgerechnet worden, sieht man es am Kernelbereich */
  uintptr_t b = (uintptr_t)v.v_baseAddr;
  if (b < 0xFFFF800000000000ULL) b = (uintptr_t)ml_io_map((vm_offset_t)b, (vm_size_t)v.v_rowBytes * v.v_height);
  if (!b) return;
  gBase = (volatile uint8_t *)b;
  gRowBytes = (uint32_t)v.v_rowBytes;
  gWidth = (uint32_t)v.v_width;
  gHeight = (uint32_t)v.v_height;
  gCols = (gWidth - 16) / kCellW;
  if (gCols > kCols) gCols = kCols;
  gOK = gHeight >= kLines * kCellH + 16;
}

static void put_px(uint32_t x, uint32_t y, bool on) {
  volatile uint32_t *p = (volatile uint32_t *)(gBase + (size_t)y * gRowBytes + (size_t)x * 4);
  *p = on ? 0x00FFFFFFu : 0x00000000u;
}

static void paint(void) {
  uint32_t x0 = 8, y0 = gHeight - kLines * kCellH - 8;
  for (unsigned l = 0; l < kLines; l++) {
    /* neueste Zeilen unten */
    int src = (int)gCount - (int)kLines + (int)l;
    const char *s = src >= 0 ? gLog[src % kLines] : "";
    for (uint32_t c = 0; c < gCols; c++) {
      char ch = c < strlen(s) ? s[c] : ' ';
      if (ch >= 'a' && ch <= 'z') ch -= 32;
      if (ch < 32 || ch > 95) ch = '?';
      const unsigned char *gl = kFont[ch - 32];
      for (unsigned ry = 0; ry < 8; ry++) {
        unsigned bits = ry < 7 ? gl[ry] : 0;
        for (unsigned rx = 0; rx < 6; rx++) {
          bool on = rx < 5 && (bits & (0x10u >> rx));
          for (unsigned sy = 0; sy < kScale; sy++)
            for (unsigned sx = 0; sx < kScale; sx++)
              put_px(x0 + c * kCellW + rx * kScale + sx, y0 + l * kCellH + ry * kScale + sy, on);
        }
      }
    }
  }
}

extern "C" void cloke_trace(const char *fmt, ...) {
  char line[kCols + 1];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(line, sizeof line, fmt, ap);
  va_end(ap);
  kprintf("ClokeTrace: %s\n", line);
  IOLog("ClokeTrace: %s\n", line); /* landet in dmesg und damit in ClokeOS/diag.txt auf dem Stick */
  if (!gInit) trace_init(false);
  strlcpy(gLog[gCount % kLines], line, sizeof gLog[0]);
  gCount++;
  if (gOK) paint();
}

/* Die gesammelten Zeilen jetzt zeigen, auch ohne cloke_trace=1, und kurz stehen lassen */
extern "C" void cloke_trace_show(unsigned seconds) {
  if (!gOK) {
    gInit = false;
    trace_init(true);
  }
  if (gOK) paint();
  if (seconds) IOSleep(seconds * 1000);
}
