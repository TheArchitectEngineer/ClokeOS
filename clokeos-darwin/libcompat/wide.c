/* Weitere libSystem-Symbole, die Apples SDK 26 bindet und PureDarwin 20.5 nicht hat
 * (gefunden mit Logic Pros libcom-api.dylib und OMF.framework, siehe docs/logic-dyld.md).
 *
 *   ___strncat_chk  die Prüfversion von strncat (FORTIFY_SOURCE); bricht wie Apples Fassung ab,
 *                   wenn das Ergebnis nicht in den Zielpuffer passt
 *   swscanf         wie sscanf auf einem wchar_t-Text; Text und Format werden nach UTF-8 gewandelt.
 *                   %ls, %lc, %S und %C (Ausgabe als wchar_t) fehlen: dafür gibt es EOF/EINVAL */
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

char *__strncat_chk(char *dst, const char *src, size_t n, size_t dstlen) {
  size_t have = strlen(dst), add = strnlen(src, n);
  if (have + add + 1 > dstlen) abort();
  memcpy(dst + have, src, add);
  dst[have + add] = 0;
  return dst;
}

/* UTF-32 -> UTF-8, nicht darstellbare Werte werden zu U+FFFD */
static char *to_utf8(const wchar_t *w) {
  size_t n = 0;
  while (w[n]) n++;
  char *out = malloc(n * 4 + 1), *p = out;
  if (!out) return NULL;
  for (size_t i = 0; i < n; i++) {
    unsigned long c = (unsigned long)w[i];
    if (c > 0x10FFFF || (c >= 0xD800 && c < 0xE000)) c = 0xFFFD;
    if (c < 0x80) *p++ = (char)c;
    else if (c < 0x800) { *p++ = 0xC0 | (c >> 6); *p++ = 0x80 | (c & 0x3F); }
    else if (c < 0x10000) { *p++ = 0xE0 | (c >> 12); *p++ = 0x80 | ((c >> 6) & 0x3F); *p++ = 0x80 | (c & 0x3F); }
    else { *p++ = 0xF0 | (c >> 18); *p++ = 0x80 | ((c >> 12) & 0x3F); *p++ = 0x80 | ((c >> 6) & 0x3F); *p++ = 0x80 | (c & 0x3F); }
  }
  *p = 0;
  return out;
}

int swscanf(const wchar_t *ws, const wchar_t *wfmt, ...) {
  char *s = to_utf8(ws), *fmt = to_utf8(wfmt);
  int r = EOF;
  if (s && fmt) {
    int wide = 0;
    for (const char *p = fmt; *p; p++) {
      if (*p != '%') continue;
      p++;
      if (*p == '%') continue;
      while (*p && !strchr("diouxXeEfFgGaAcsSCpn[", *p)) {
        if (*p == 'l' && (p[1] == 's' || p[1] == 'c')) wide = 1;
        p++;
      }
      if (!*p) break;
      if (*p == 'S' || *p == 'C') wide = 1;
    }
    if (wide) errno = EINVAL;
    else {
      va_list ap;
      va_start(ap, wfmt);
      r = vsscanf(s, fmt, ap);
      va_end(ap);
    }
  }
  free(s);
  free(fmt);
  return r;
}
