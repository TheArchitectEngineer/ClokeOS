/* CloakServer: Programme als .app-Bundles wie bei macOS.
 *
 * Ein Programm ist ein Ordner Name.app mit Contents/Info.plist, Contents/MacOS/<Startdatei> und
 * Contents/Resources/<Icon>, genau wie bei macOS. CloakServer findet sie in /System/Applications,
 * /Applications und ~/Applications. Installieren heißt: das Bundle in einen dieser Ordner
 * kopieren. Die Info.plist darf XML oder binär sein (bplist00), das Icon SVG, PNG oder .icns.
 *
 * Mac-Programme (Mach-O) prüft bundle_read, bevor sie starten: Für welche Architektur sind sie
 * gebaut, und gibt es alle Bibliotheken und Frameworks, die sie laden? AppKit fehlt PureDarwin
 * noch; solche Programme kann CloakServer erkennen und anzeigen, aber nicht ausführen. */
#include "cloakserver.h"

#include <ctype.h>
#include <dirent.h>
#include <libgen.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

unsigned char *read_bytes(const char *path, size_t *len) {
  FILE *f = fopen(path, "rb");
  if (!f) return NULL;
  size_t cap = 1 << 14, n = 0;
  unsigned char *buf = malloc(cap + 1);
  for (size_t k; buf && (k = fread(buf + n, 1, cap - n, f)) > 0;) {
    n += k;
    if (n == cap) {
      if (cap >= (64u << 20)) break; /* mehr als 64 MiB liest hier niemand */
      unsigned char *nb = realloc(buf, cap * 2 + 1);
      if (!nb) break;
      buf = nb;
      cap *= 2;
    }
  }
  fclose(f);
  if (buf) buf[n] = 0;
  if (len) *len = n;
  return buf;
}

/* ---------------------------------------------------------------------------------------- */
/* Property Lists                                                                             */

static void xml_unescape(char *s) {
  static const struct { const char *e; char c; } ent[] = {
    {"&amp;", '&'}, {"&lt;", '<'}, {"&gt;", '>'}, {"&quot;", '"'}, {"&apos;", '\''}};
  char *w = s;
  for (char *r = s; *r;) {
    bool done = false;
    for (size_t i = 0; i < sizeof ent / sizeof ent[0] && *r == '&'; i++)
      if (!strncmp(r, ent[i].e, strlen(ent[i].e))) {
        *w++ = ent[i].c;
        r += strlen(ent[i].e);
        done = true;
        break;
      }
    if (!done) *w++ = *r++;
  }
  *w = 0;
}

/* Wert nach <key>key</key>: <string>, <integer>, <true/>, <false/> */
static bool xml_get(const char *xml, const char *key, char *out, size_t n) {
  char pat[160];
  snprintf(pat, sizeof pat, "<key>%s</key>", key);
  const char *p = strstr(xml, pat);
  if (!p) return false;
  p += strlen(pat);
  while (isspace((unsigned char)*p)) p++;
  if (!strncmp(p, "<true/>", 7) || !strncmp(p, "<false/>", 8)) {
    snprintf(out, n, "%s", p[1] == 't' ? "1" : "0");
    return true;
  }
  if (!strncmp(p, "<string/>", 9)) {
    if (n) out[0] = 0;
    return true;
  }
  const char *tags[] = {"string", "integer", "real"};
  for (int i = 0; i < 3; i++) {
    char open[16], close[16];
    snprintf(open, sizeof open, "<%s>", tags[i]);
    snprintf(close, sizeof close, "</%s>", tags[i]);
    if (strncmp(p, open, strlen(open))) continue;
    p += strlen(open);
    const char *e = strstr(p, close);
    if (!e) return false;
    snprintf(out, n, "%.*s", (int)(e - p), p);
    xml_unescape(out);
    return true;
  }
  return false;
}

static int xml_get_array(const char *xml, const char *key, char out[][128], int max) {
  char pat[160];
  snprintf(pat, sizeof pat, "<key>%s</key>", key);
  const char *p = strstr(xml, pat);
  if (!p) return -1;
  p += strlen(pat);
  while (isspace((unsigned char)*p)) p++;
  if (!strncmp(p, "<array/>", 8)) return 0;
  if (strncmp(p, "<array>", 7)) return -1;
  const char *end = strstr(p, "</array>");
  int k = 0;
  for (const char *s = strstr(p, "<string>"); s && (!end || s < end) && k < max; s = strstr(s, "<string>")) {
    s += 8;
    const char *e = strstr(s, "</string>");
    if (!e) break;
    snprintf(out[k], 128, "%.*s", (int)(e - s), s);
    xml_unescape(out[k++]);
    s = e;
  }
  return k;
}

/* Binäre Property List (bplist00), wie Xcode sie manchmal für Info.plist schreibt. Gelesen wird
 * nur, was ein Bundle braucht: das oberste Wörterbuch mit Text, Zahlen und Wahrheitswerten. */
typedef struct {
  const unsigned char *d;
  size_t len;
  int off_size, ref_size;
  uint64_t nobj, top, table;
} BPlist;

static uint64_t be(const unsigned char *p, int n) {
  uint64_t v = 0;
  for (int i = 0; i < n; i++) v = v << 8 | p[i];
  return v;
}

static bool bp_open(BPlist *b, const unsigned char *d, size_t len) {
  if (len < 40 || memcmp(d, "bplist00", 8)) return false;
  const unsigned char *t = d + len - 32;
  b->d = d;
  b->len = len;
  b->off_size = t[6];
  b->ref_size = t[7];
  b->nobj = be(t + 8, 8);
  b->top = be(t + 16, 8);
  b->table = be(t + 24, 8);
  return b->off_size >= 1 && b->off_size <= 8 && b->ref_size >= 1 && b->ref_size <= 8 &&
         b->table + b->nobj * b->off_size <= len - 32 && b->top < b->nobj;
}

static const unsigned char *bp_obj(const BPlist *b, uint64_t ref) {
  if (ref >= b->nobj) return NULL;
  uint64_t off = be(b->d + b->table + ref * b->off_size, b->off_size);
  return off < b->len - 32 ? b->d + off : NULL;
}

/* Länge aus dem Kopfbyte; bei 0xF folgt sie als eigene Zahl */
static const unsigned char *bp_count(const unsigned char *o, uint64_t *n) {
  *n = o[0] & 0xf;
  if (*n != 0xf) return o + 1;
  int sz = 1 << (o[1] & 0xf);
  *n = be(o + 2, sz);
  return o + 2 + sz;
}

static bool bp_string(const BPlist *b, const unsigned char *o, char *out, size_t n) {
  if (!o || !n) return false;
  uint64_t cnt;
  const unsigned char *p;
  switch (o[0] >> 4) {
  case 0x5: /* ASCII */
    p = bp_count(o, &cnt);
    if (p + cnt > b->d + b->len) return false;
    snprintf(out, n, "%.*s", (int)cnt, p);
    return true;
  case 0x6: { /* UTF-16BE nach UTF-8 */
    p = bp_count(o, &cnt);
    if (p + 2 * cnt > b->d + b->len) return false;
    size_t w = 0;
    for (uint64_t i = 0; i < cnt && w + 4 < n; i++) {
      unsigned c = (unsigned)be(p + 2 * i, 2);
      if (c < 0x80) out[w++] = (char)c;
      else if (c < 0x800) out[w++] = (char)(0xc0 | c >> 6), out[w++] = (char)(0x80 | (c & 0x3f));
      else out[w++] = (char)(0xe0 | c >> 12), out[w++] = (char)(0x80 | (c >> 6 & 0x3f)), out[w++] = (char)(0x80 | (c & 0x3f));
    }
    out[w] = 0;
    return true;
  }
  case 0x1: /* Zahl */
    snprintf(out, n, "%lld", (long long)be(o + 1, 1 << (o[0] & 0xf)));
    return true;
  case 0x0:
    if (o[0] == 0x08 || o[0] == 0x09) {
      snprintf(out, n, "%s", o[0] == 0x09 ? "1" : "0");
      return true;
    }
  }
  return false;
}

static bool bp_get(const unsigned char *d, size_t len, const char *key, char *out, size_t n) {
  BPlist b;
  if (!bp_open(&b, d, len)) return false;
  const unsigned char *o = bp_obj(&b, b.top);
  if (!o || o[0] >> 4 != 0xd) return false;
  uint64_t cnt;
  const unsigned char *refs = bp_count(o, &cnt);
  if (refs + 2 * cnt * b.ref_size > d + len) return false;
  for (uint64_t i = 0; i < cnt; i++) {
    char k[160];
    if (!bp_string(&b, bp_obj(&b, be(refs + i * b.ref_size, b.ref_size)), k, sizeof k) || strcmp(k, key)) continue;
    return bp_string(&b, bp_obj(&b, be(refs + (cnt + i) * b.ref_size, b.ref_size)), out, n);
  }
  return false;
}

bool plist_get(const unsigned char *data, size_t len, const char *key, char *out, size_t n) {
  if (!data) return false;
  if (len >= 8 && !memcmp(data, "bplist00", 8)) return bp_get(data, len, key, out, n);
  return xml_get((const char *)data, key, out, n);
}

int plist_get_array(const unsigned char *data, const char *key, char out[][128], int max) {
  if (!data || !memcmp(data, "bplist", 6)) return -1;
  return xml_get_array((const char *)data, key, out, max);
}

static void xml_put(FILE *f, const char *s) {
  for (; *s; s++) {
    if (*s == '&') fputs("&amp;", f);
    else if (*s == '<') fputs("&lt;", f);
    else if (*s == '>') fputs("&gt;", f);
    else fputc(*s, f);
  }
}

bool plist_write(const char *path, const PlistItem *items, int n) {
  char tmp[1100];
  snprintf(tmp, sizeof tmp, "%s.neu", path);
  FILE *f = fopen(tmp, "w");
  if (!f) return false;
  fputs("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        "<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" \"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n"
        "<plist version=\"1.0\">\n<dict>\n", f);
  for (int i = 0; i < n; i++) {
    fputs("\t<key>", f);
    xml_put(f, items[i].key);
    fputs("</key>\n", f);
    if (items[i].arr) {
      fputs("\t<array>\n", f);
      for (int k = 0; k < items[i].narr; k++) {
        fputs("\t\t<string>", f);
        xml_put(f, items[i].arr[k]);
        fputs("</string>\n", f);
      }
      fputs("\t</array>\n", f);
    } else if (items[i].s) {
      fputs("\t<string>", f);
      xml_put(f, items[i].s);
      fputs("</string>\n", f);
    } else {
      fprintf(f, "\t<integer>%ld</integer>\n", items[i].i);
    }
  }
  fputs("</dict>\n</plist>\n", f);
  bool ok = !ferror(f);
  ok &= fclose(f) == 0;
  return ok && rename(tmp, path) == 0;
}

/* ---------------------------------------------------------------------------------------- */
/* Mach-O: Architektur und geladene Bibliotheken                                              */

#define MH_MAGIC_64 0xfeedfacfu
#define FAT_MAGIC   0xcafebabeu
#define CPU_X86_64  0x01000007u
#define CPU_ARM64   0x0100000cu
#define CPU_SUBTYPE_ARM64E 2u
/* Die Architektur, für die CloakServer gebaut ist, ist die von ClokeOS. Die andere läuft nicht:
 * ClokeOS hat weder Rosetta noch einen arm64-Emulator. */
#if defined(__arm64__) || defined(__aarch64__)
#define CPU_NATIVE  CPU_ARM64
#define CPU_FOREIGN CPU_X86_64
#define NATIVE_NAME "arm64"
#else
#define CPU_NATIVE  CPU_X86_64
#define CPU_FOREIGN CPU_ARM64
#define NATIVE_NAME "x86_64"
#endif
#define LC_LOAD_DYLIB        0xcu
#define LC_REEXPORT_DYLIB    0x8000001fu
#define LC_LOAD_UPWARD_DYLIB 0x80000023u

static uint32_t le32(const unsigned char *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }

/* "…/AppKit.framework/Versions/C/AppKit" -> "AppKit.framework", "/usr/lib/libfoo.dylib" -> "libfoo.dylib" */
static void lib_name(const char *path, char *out, size_t n) {
  const char *fw = strstr(path, ".framework/");
  if (fw) {
    const char *s = fw;
    while (s > path && s[-1] != '/') s--;
    snprintf(out, n, "%.*s", (int)(fw + 10 - s), s);
  } else {
    const char *s = strrchr(path, '/');
    snprintf(out, n, "%s", s ? s + 1 : path);
  }
}

static void check_thin(Bundle *b, const unsigned char *m, size_t len) {
  if (len < 32 || le32(m) != MH_MAGIC_64) {
    b->kind = APP_NOEXEC;
    snprintf(b->missing, sizeof b->missing, "keine ausführbare Datei");
    return;
  }
  uint32_t cpu = le32(m + 4), ncmds = le32(m + 16), size = le32(m + 20);
  b->mac = true;
  if (cpu == CPU_FOREIGN) {
    b->kind = APP_ARCH;
    return;
  }
#if defined(__arm64__) || defined(__aarch64__)
  /* arm64e (Zeigersignaturen) lehnt PureDarwins Kernel ab: "bad CPU type in executable" */
  if ((le32(m + 8) & 0xffffff) == CPU_SUBTYPE_ARM64E) {
    b->kind = APP_ARM64E;
    return;
  }
#endif
  if (cpu != CPU_NATIVE) {
    b->kind = APP_NOEXEC;
    snprintf(b->missing, sizeof b->missing, "unbekannte Architektur");
    return;
  }
  if (32 + (size_t)size > len) return;
  char dir[1024];
  snprintf(dir, sizeof dir, "%s", b->exec);
  char *slash = strrchr(dir, '/');
  if (slash) *slash = 0;
  const unsigned char *p = m + 32, *end = m + 32 + size;
  for (uint32_t i = 0; i < ncmds && p + 8 <= end; i++) {
    uint32_t cmd = le32(p), cs = le32(p + 4);
    if (cs < 8 || p + cs > end) break;
    if ((cmd == LC_LOAD_DYLIB || cmd == LC_REEXPORT_DYLIB || cmd == LC_LOAD_UPWARD_DYLIB) && cs > 24) {
      uint32_t off = le32(p + 8);
      if (off < cs) {
        char path[1024];
        snprintf(path, sizeof path, "%.*s", (int)strnlen((const char *)p + off, cs - off), (const char *)p + off);
        char real[1100];
        if (!strncmp(path, "@executable_path/", 17) || !strncmp(path, "@loader_path/", 13))
          snprintf(real, sizeof real, "%s/%s", dir, strchr(path, '/') + 1);
        else
          snprintf(real, sizeof real, "%s", path);
        /* @rpath hängt von LC_RPATH ab; das prüft erst dyld beim Start */
        if (path[0] != '@' || real[0] == '/') {
          /* Ein Framework, das im System fehlt, findet dyld mit DYLD_FRAMEWORK_PATH in den
           * Mac-Frameworks (…/AppKit.framework/Versions/C/AppKit -> MAC_FRAMEWORKS/AppKit.framework/…) */
          const char *fw = strstr(path, ".framework/");
          if (fw && access(real, F_OK)) {
            const char *s = fw;
            while (s > path && s[-1] != '/') s--;
            char alt[1100];
            snprintf(alt, sizeof alt, "%s/%s", MAC_FRAMEWORKS, s);
            if (!access(alt, F_OK)) {
              snprintf(real, sizeof real, "%s", alt);
              b->macfw = true;
            }
          }
          if (access(real, F_OK) && b->kind == APP_OK) {
            lib_name(path, b->missing, sizeof b->missing);
            b->kind = (strstr(path, "/AppKit.framework/") || strstr(path, "/Cocoa.framework/") ||
                       strstr(path, "/SwiftUI.framework/") || strstr(path, "/Carbon.framework/"))
                        ? APP_APPKIT
                        : APP_MISSING;
          }
        }
      }
    }
    p += cs;
  }
}

static void check_exec(Bundle *b) {
  size_t len = 0;
  unsigned char *m = read_bytes(b->exec, &len);
  if (!m) {
    b->kind = APP_NOEXEC;
    snprintf(b->missing, sizeof b->missing, "Contents/MacOS/%s", b->exec_name);
    return;
  }
  if (len >= 2 && m[0] == '#' && m[1] == '!') { /* Startskript: Programm für ClokeOS */
    if (access(b->exec, X_OK)) {
      b->kind = APP_NOEXEC;
      snprintf(b->missing, sizeof b->missing, "Startdatei nicht ausführbar");
    }
  } else if (len >= 8 && be(m, 4) == FAT_MAGIC) { /* Universal: den Teil für ClokeOS suchen */
    uint32_t n = (uint32_t)be(m + 4, 4);
    bool foreign = false;
    const unsigned char *pick = NULL;
    uint32_t pick_sz = 0;
    for (uint32_t i = 0; i < n && 8 + (i + 1) * 20 <= len; i++) {
      const unsigned char *a = m + 8 + i * 20;
      uint32_t cpu = (uint32_t)be(a, 4), sub = (uint32_t)be(a + 4, 4) & 0xffffff;
      uint32_t off = (uint32_t)be(a + 8, 4), sz = (uint32_t)be(a + 12, 4);
      if (cpu == CPU_FOREIGN) foreign = true;
      /* wie dyld: arm64 vor arm64e, das nur Apples eigene Programme brauchen */
      if (cpu == CPU_NATIVE && (size_t)off + sz <= len && (!pick || sub != CPU_SUBTYPE_ARM64E)) {
        pick = m + off;
        pick_sz = sz;
      }
    }
    if (pick) {
      check_thin(b, pick, pick_sz);
    } else {
      b->mac = true;
      b->kind = foreign ? APP_ARCH : APP_NOEXEC;
      if (!foreign) snprintf(b->missing, sizeof b->missing, "kein " NATIVE_NAME "-Teil");
    }
  } else {
    check_thin(b, m, len);
  }
  free(m);
}

/* ---------------------------------------------------------------------------------------- */
/* Bundle lesen                                                                               */

static void find_icon(Bundle *b, const char *name) {
  if (!name || !name[0]) return;
  static const char *ext[] = {"", ".svg", ".png", ".icns"};
  for (int i = 0; i < 4; i++) {
    char p[1100];
    snprintf(p, sizeof p, "%s/Contents/Resources/%s%s", b->path, name, ext[i]);
    struct stat st;
    if (!stat(p, &st) && S_ISREG(st.st_mode)) {
      snprintf(b->icon, sizeof b->icon, "%s", p);
      return;
    }
  }
}

/* Zeitpunkt der letzten Änderung: Ordner oder Info.plist, was neuer ist */
long bundle_mtime(const char *path) {
  struct stat st;
  long t = stat(path, &st) ? 0 : (long)st.st_mtime;
  char info[1100];
  snprintf(info, sizeof info, "%s/Contents/Info.plist", path);
  if (!stat(info, &st) && (long)st.st_mtime > t) t = (long)st.st_mtime;
  return t;
}

bool bundle_read(const char *path, Bundle *b) {
  memset(b, 0, sizeof *b);
  snprintf(b->path, sizeof b->path, "%s", path);
  struct stat st;
  if (stat(path, &st) || !S_ISDIR(st.st_mode)) return false;
  b->mtime = bundle_mtime(path);

  char base[256];
  snprintf(base, sizeof base, "%s", strrchr(path, '/') ? strrchr(path, '/') + 1 : path);
  char *dot = strstr(base, ".app");
  if (dot) *dot = 0;

  char info[1100];
  snprintf(info, sizeof info, "%s/Contents/Info.plist", path);
  size_t len = 0;
  unsigned char *pl = read_bytes(info, &len);
  char v[1024];
  if (plist_get(pl, len, "CFBundleDisplayName", v, sizeof v) && v[0]) snprintf(b->name, sizeof b->name, "%s", v);
  else if (plist_get(pl, len, "CFBundleName", v, sizeof v) && v[0]) snprintf(b->name, sizeof b->name, "%s", v);
  else snprintf(b->name, sizeof b->name, "%s", base);
  if (plist_get(pl, len, "CFBundleIdentifier", v, sizeof v)) snprintf(b->ident, sizeof b->ident, "%s", v);
  if (plist_get(pl, len, "CFBundleExecutable", v, sizeof v) && v[0]) snprintf(b->exec_name, sizeof b->exec_name, "%s", v);
  else snprintf(b->exec_name, sizeof b->exec_name, "%s", base);
  if (plist_get(pl, len, "CLWindowClasses", v, sizeof v)) snprintf(b->classes, sizeof b->classes, "%s", v);
  if (plist_get(pl, len, "CLInternal", v, sizeof v)) snprintf(b->internal, sizeof b->internal, "%s", v);
  if (plist_get(pl, len, "LSUIElement", v, sizeof v) && (!strcmp(v, "1") || !strcasecmp(v, "true"))) b->hidden = true;
  if (plist_get(pl, len, "CFBundleIconFile", v, sizeof v)) find_icon(b, v);
  if (!b->icon[0] && plist_get(pl, len, "CFBundleIconName", v, sizeof v)) find_icon(b, v);
  if (!b->icon[0]) find_icon(b, "AppIcon");
  free(pl);

  snprintf(b->exec, sizeof b->exec, "%s/Contents/MacOS/%s", path, b->exec_name);
  if (!b->internal[0]) check_exec(b);
  return true;
}

/* PNG aus einer .icns-Datei: das kleinste Bild ab min Pixeln, sonst das größte */
unsigned char *icns_png(const char *path, int min, size_t *out_len) {
  size_t len = 0;
  unsigned char *d = read_bytes(path, &len);
  if (!d) return NULL;
  static const struct { const char *t; int px; } types[] = {
    {"ic07", 128}, {"ic13", 256}, {"ic08", 256}, {"ic14", 512}, {"ic09", 512}, {"ic10", 1024}, {"icp6", 64}, {"icp5", 32}};
  const unsigned char *best = NULL;
  size_t best_len = 0;
  int best_px = 0;
  if (len >= 8 && !memcmp(d, "icns", 4)) {
    for (size_t o = 8; o + 8 <= len;) {
      uint32_t el = (uint32_t)be(d + o + 4, 4);
      if (el < 8 || o + el > len) break;
      const unsigned char *data = d + o + 8;
      size_t dl = el - 8;
      if (dl > 8 && !memcmp(data, "\x89PNG", 4))
        for (size_t i = 0; i < sizeof types / sizeof types[0]; i++)
          if (!memcmp(d + o, types[i].t, 4)) {
            int px = types[i].px;
            bool better = !best || (best_px < min ? px > best_px : px >= min && px < best_px);
            if (better) best = data, best_len = dl, best_px = px;
          }
      o += el;
    }
  }
  unsigned char *png = NULL;
  if (best) {
    png = malloc(best_len);
    if (png) memcpy(png, best, best_len);
    *out_len = best_len;
  }
  free(d);
  return png;
}
