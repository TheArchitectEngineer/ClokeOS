/*
 * clokeos-dmg: öffnet macOS-Disk-Images (UDIF, .dmg) und legt ihren Inhalt unter /Volumes ab.
 *
 *   clokeos-dmg mount <datei.dmg>     Image lesen, HFS+-Volume auspacken nach /Volumes/<Name>
 *   clokeos-dmg eject <Ordner>        ausgepacktes Volume wieder entfernen
 *   clokeos-dmg list                  Volumes, die von Images stammen
 *
 * Ausgabe zeilenweise: STEP <text> | MOUNTED <pfad> | EJECTED <pfad> | ERROR <text>
 *
 * Warum auspacken statt einhängen: PureDarwin hat keinen Treiber für Image-Dateien (kein hdiutil, kein vn-Gerät),
 * und der Kernel hängt HFS+ nur von einem Blockgerät ein. Deshalb liest dieses Programm das Image selbst:
 *   1. UDIF: 512-Byte-Anhang "koly", die XML-Liste (blkx) mit den Blocktabellen "mish"; Blöcke roh, nullen,
 *      zlib (UDZO) und bzip2 (UDBZ) werden entpackt (eigener Inflate- und bunzip2-Code, PureDarwin hat nur libz).
 *   2. Das entpackte Image liegt als dünn besetzte Datei in /var/tmp; darin findet das Programm das HFS+-Volume
 *      (blank, in einer Apple-Partitionstabelle oder in einer GPT).
 *   3. HFS+ (auch HFSX): Volumekopf, Extents-Überlauf, B-Bäume (Katalog, Attribute); Ordner, Dateien, Symlinks,
 *      harte Links, Rechte und Zeiten werden nach /Volumes/<Name> kopiert, Dateien mit zlib-Kompression
 *      (decmpfs Typ 3 und 4) entpackt. Resource Forks entfallen. Das Volume ist danach schreibgeschützt.
 * Das Programm läuft auch auf dem Mac (zum Prüfen); CLOKE_VOLUMES ersetzt /Volumes.
 */
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>

#define MARK ".clokeos-dmg"

static void out(const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  vprintf(fmt, ap);
  va_end(ap);
  putchar('\n');
  fflush(stdout);
}

static void fail(const char *fmt, ...) {
  char msg[400];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(msg, sizeof msg, fmt, ap);
  va_end(ap);
  out("ERROR %s", msg);
  exit(1);
}

static uint16_t be16(const uint8_t *p) { return (uint16_t)(p[0] << 8 | p[1]); }
static uint32_t be32(const uint8_t *p) { return (uint32_t)p[0] << 24 | p[1] << 16 | p[2] << 8 | p[3]; }
static uint64_t be64(const uint8_t *p) { return (uint64_t)be32(p) << 32 | be32(p + 4); }

/* ---- Inflate (RFC 1950/1951), nach dem Aufbau von zlibs puff.c -------------------------------- */

typedef struct {
  const uint8_t *in;
  size_t inlen, inpos;
  uint32_t bitbuf;
  int bitcnt;
  bool err;
} Inf;

static int inf_bits(Inf *s, int need) {
  uint32_t v = s->bitbuf;
  while (s->bitcnt < need) {
    if (s->inpos >= s->inlen) {
      s->err = true;
      return 0;
    }
    v |= (uint32_t)s->in[s->inpos++] << s->bitcnt;
    s->bitcnt += 8;
  }
  s->bitbuf = v >> need;
  s->bitcnt -= need;
  return (int)(v & ((1u << need) - 1));
}

typedef struct {
  short count[16], symbol[288];
} Huf;

static int inf_decode(Inf *s, const Huf *h) {
  int code = 0, first = 0, index = 0;
  for (int len = 1; len <= 15; len++) {
    code |= inf_bits(s, 1);
    if (s->err) return -1;
    int count = h->count[len];
    if (code - count < first) return h->symbol[index + (code - first)];
    index += count;
    first += count;
    first <<= 1;
    code <<= 1;
  }
  return -1;
}

static int huf_build(Huf *h, const short *length, int n) {
  short offs[16];
  for (int i = 0; i < 16; i++) h->count[i] = 0;
  for (int i = 0; i < n; i++) h->count[length[i]]++;
  if (h->count[0] == n) return 0;
  int left = 1;
  for (int len = 1; len <= 15; len++) {
    left <<= 1;
    left -= h->count[len];
    if (left < 0) return left;
  }
  offs[1] = 0;
  for (int len = 1; len < 15; len++) offs[len + 1] = offs[len] + h->count[len];
  for (int i = 0; i < n; i++)
    if (length[i]) h->symbol[offs[length[i]]++] = (short)i;
  return left;
}

static const short LBASE[] = {3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
static const short LEXT[] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
static const short DBASE[] = {1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
static const short DEXT[] = {0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};

static bool inf_codes(Inf *s, uint8_t *o, size_t olen, size_t *op, const Huf *lc, const Huf *dc) {
  for (;;) {
    int sym = inf_decode(s, lc);
    if (sym < 0) return false;
    if (sym < 256) {
      if (*op >= olen) return false;
      o[(*op)++] = (uint8_t)sym;
    } else if (sym == 256) {
      return true;
    } else {
      sym -= 257;
      if (sym >= 29) return false;
      int len = LBASE[sym] + inf_bits(s, LEXT[sym]);
      int ds = inf_decode(s, dc);
      if (ds < 0 || ds >= 30) return false;
      size_t dist = (size_t)DBASE[ds] + inf_bits(s, DEXT[ds]);
      if (s->err || dist > *op || *op + len > olen) return false;
      for (int i = 0; i < len; i++, (*op)++) o[*op] = o[*op - dist];
    }
  }
}

/* zlib-Strom in out (olen Byte) entpacken; gibt die Zahl der Bytes zurück, -1 bei Fehler */
static long zlib_inflate(const uint8_t *in, size_t inlen, uint8_t *o, size_t olen) {
  if (inlen < 2 || (in[0] & 0x0f) != 8) return -1;
  Inf s = {.in = in + 2, .inlen = inlen - 2};
  size_t op = 0;
  int last;
  do {
    last = inf_bits(&s, 1);
    int type = inf_bits(&s, 2);
    if (s.err) return -1;
    if (type == 0) {
      s.bitbuf = 0, s.bitcnt = 0;
      if (s.inpos + 4 > s.inlen) return -1;
      unsigned len = s.in[s.inpos] | s.in[s.inpos + 1] << 8;
      s.inpos += 4;
      if (s.inpos + len > s.inlen || op + len > olen) return -1;
      memcpy(o + op, s.in + s.inpos, len);
      s.inpos += len;
      op += len;
    } else if (type == 1 || type == 2) {
      Huf lc, dc;
      short lengths[320];
      if (type == 1) {
        int i = 0;
        for (; i < 144; i++) lengths[i] = 8;
        for (; i < 256; i++) lengths[i] = 9;
        for (; i < 280; i++) lengths[i] = 7;
        for (; i < 288; i++) lengths[i] = 8;
        huf_build(&lc, lengths, 288);
        for (i = 0; i < 30; i++) lengths[i] = 5;
        huf_build(&dc, lengths, 30);
      } else {
        int nlen = inf_bits(&s, 5) + 257, ndist = inf_bits(&s, 5) + 1, ncode = inf_bits(&s, 4) + 4;
        static const short order[19] = {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};
        if (nlen > 286 || ndist > 30) return -1;
        int i = 0;
        for (; i < ncode; i++) lengths[order[i]] = (short)inf_bits(&s, 3);
        for (; i < 19; i++) lengths[order[i]] = 0;
        if (huf_build(&lc, lengths, 19) != 0) return -1;
        for (i = 0; i < nlen + ndist;) {
          int sym = inf_decode(&s, &lc);
          if (sym < 0) return -1;
          if (sym < 16) {
            lengths[i++] = (short)sym;
          } else {
            int len = 0, rep;
            if (sym == 16) {
              if (i == 0) return -1;
              len = lengths[i - 1];
              rep = 3 + inf_bits(&s, 2);
            } else if (sym == 17) {
              rep = 3 + inf_bits(&s, 3);
            } else {
              rep = 11 + inf_bits(&s, 7);
            }
            if (i + rep > nlen + ndist) return -1;
            while (rep--) lengths[i++] = (short)len;
          }
        }
        if (lengths[256] == 0) return -1;
        int err = huf_build(&lc, lengths, nlen);
        if (err && (err < 0 || nlen != lc.count[0] + lc.count[1])) return -1;
        err = huf_build(&dc, lengths + nlen, ndist);
        if (err && (err < 0 || ndist != dc.count[0] + dc.count[1])) return -1;
      }
      if (!inf_codes(&s, o, olen, &op, &lc, &dc)) return -1;
    } else {
      return -1;
    }
  } while (!last);
  return (long)op;
}

/* ---- bunzip2 ----------------------------------------------------------------------------------- */

typedef struct {
  const uint8_t *in;
  size_t n, pos;
  uint32_t buf;
  int cnt;
  bool err;
} Bz;

static uint32_t bz_bits(Bz *b, int k) {
  while (b->cnt < k) {
    if (b->pos >= b->n) {
      b->err = true;
      return 0;
    }
    b->buf = b->buf << 8 | b->in[b->pos++];
    b->cnt += 8;
  }
  b->cnt -= k;
  return (b->buf >> b->cnt) & ((1u << k) - 1);
}

typedef struct {
  int minl, maxl, limit[23], base[23], perm[258];
} BzHuff;

static void bz_huff(BzHuff *h, const uint8_t *len, int alpha) {
  int minl = 32, maxl = 0, pp = 0;
  for (int i = 0; i < alpha; i++) {
    if (len[i] > maxl) maxl = len[i];
    if (len[i] < minl) minl = len[i];
  }
  h->minl = minl, h->maxl = maxl;
  for (int i = minl; i <= maxl; i++)
    for (int j = 0; j < alpha; j++)
      if (len[j] == i) h->perm[pp++] = j;
  for (int i = 0; i < 23; i++) h->base[i] = h->limit[i] = 0;
  for (int i = 0; i < alpha; i++) h->base[len[i] + 1]++;
  for (int i = 1; i < 23; i++) h->base[i] += h->base[i - 1];
  int vec = 0;
  for (int i = minl; i <= maxl; i++) {
    vec += h->base[i + 1] - h->base[i];
    h->limit[i] = vec - 1;
    vec <<= 1;
  }
  for (int i = minl + 1; i <= maxl; i++) h->base[i] = ((h->limit[i - 1] + 1) << 1) - h->base[i];
}

static int bz_sym(Bz *b, const BzHuff *h) {
  int zn = h->minl;
  int zvec = (int)bz_bits(b, zn);
  while (zn <= 20 && zvec > h->limit[zn]) {
    zn++;
    zvec = zvec << 1 | (int)bz_bits(b, 1);
    if (b->err) return -1;
  }
  if (zn > 20 || zvec - h->base[zn] < 0 || zvec - h->base[zn] >= 258) return -1;
  return h->perm[zvec - h->base[zn]];
}

/* Ein bzip2-Strom ("BZh…") in o (olen Byte); Länge oder -1 */
static long bz_decompress(const uint8_t *in, size_t inlen, uint8_t *o, size_t olen) {
  Bz b = {.in = in, .n = inlen};
  if (bz_bits(&b, 8) != 'B' || bz_bits(&b, 8) != 'Z' || bz_bits(&b, 8) != 'h') return -1;
  int level = (int)bz_bits(&b, 8) - '0';
  if (level < 1 || level > 9) return -1;
  uint32_t *tt = malloc((size_t)level * 100000 * 4);
  if (!tt) return -1;
  size_t op = 0;
  long ret = -1;
  for (;;) {
    uint32_t m1 = bz_bits(&b, 24), m2 = bz_bits(&b, 24);
    if (b.err) goto done;
    if (m1 == 0x177245 && m2 == 0x385090) break; /* Ende des Stroms */
    if (m1 != 0x314159 || m2 != 0x265359) goto done;
    bz_bits(&b, 32); /* CRC des Blocks */
    if (bz_bits(&b, 1)) goto done; /* zufällig gemischte Blöcke gibt es seit bzip2 0.9.5 nicht mehr */
    uint32_t orig = bz_bits(&b, 24);
    uint8_t used[256], seq[256];
    int nused = 0;
    uint32_t map16 = bz_bits(&b, 16);
    for (int i = 0; i < 16; i++)
      if (map16 & (0x8000u >> i)) {
        uint32_t m = bz_bits(&b, 16);
        for (int j = 0; j < 16; j++)
          if (m & (0x8000u >> j)) used[nused++] = (uint8_t)(i * 16 + j);
      }
    if (!nused) goto done;
    int alpha = nused + 2, ngroups = (int)bz_bits(&b, 3), nsel = (int)bz_bits(&b, 15);
    if (ngroups < 2 || ngroups > 6 || nsel < 1) goto done;
    uint8_t *sel = malloc((size_t)nsel);
    if (!sel) goto done;
    uint8_t mtf[6] = {0, 1, 2, 3, 4, 5};
    for (int i = 0; i < nsel; i++) {
      int j = 0;
      while (bz_bits(&b, 1)) {
        if (++j >= ngroups || b.err) {
          free(sel);
          goto done;
        }
      }
      uint8_t v = mtf[j];
      for (; j > 0; j--) mtf[j] = mtf[j - 1];
      mtf[0] = v;
      sel[i] = v;
    }
    BzHuff hf[6];
    for (int g = 0; g < ngroups; g++) {
      uint8_t len[258];
      int cur = (int)bz_bits(&b, 5);
      for (int i = 0; i < alpha; i++) {
        for (;;) {
          if (cur < 1 || cur > 20 || b.err) {
            free(sel);
            goto done;
          }
          if (!bz_bits(&b, 1)) break;
          cur += bz_bits(&b, 1) ? -1 : 1;
        }
        len[i] = (uint8_t)cur;
      }
      bz_huff(&hf[g], len, alpha);
    }
    for (int i = 0; i < 256; i++) seq[i] = (uint8_t)i;
    int unz[256] = {0}, eob = nused + 1, nblock = 0, gsel = 0, gleft = 0;
    BzHuff *cur = NULL;
    int sym, run_n = 0, run_es = 0;
    bool bad = false;
    for (;;) {
      if (!gleft) {
        if (gsel >= nsel) {
          bad = true;
          break;
        }
        cur = &hf[sel[gsel++]];
        gleft = 50;
      }
      gleft--;
      sym = bz_sym(&b, cur);
      if (sym < 0 || b.err) {
        bad = true;
        break;
      }
      if (sym <= 1) { /* RUNA, RUNB */
        if (!run_n) run_n = 1, run_es = -1;
        run_es += (sym + 1) * run_n;
        run_n <<= 1;
        if (run_es > level * 100000) {
          bad = true;
          break;
        }
        continue;
      }
      if (run_n) {
        int es = run_es + 1;
        uint8_t uc = used[seq[0]];
        if (nblock + es > level * 100000) {
          bad = true;
          break;
        }
        unz[uc] += es;
        while (es--) tt[nblock++] = uc;
        run_n = 0;
      }
      if (sym == eob) break;
      int nn = sym - 1;
      uint8_t v = seq[nn];
      for (; nn > 0; nn--) seq[nn] = seq[nn - 1];
      seq[0] = v;
      if (nblock >= level * 100000) {
        bad = true;
        break;
      }
      uint8_t uc = used[v];
      unz[uc]++;
      tt[nblock++] = uc;
    }
    free(sel);
    if (bad || orig >= (uint32_t)nblock) goto done;
    int cf[257];
    cf[0] = 0;
    for (int i = 1; i < 257; i++) cf[i] = cf[i - 1] + unz[i - 1];
    for (int i = 0; i < nblock; i++) {
      uint8_t uc = tt[i] & 0xff;
      tt[cf[uc]++] |= (uint32_t)i << 8;
    }
    uint32_t tpos = tt[orig] >> 8;
    int prev = -1, same = 0;
    for (int i = 0; i < nblock; i++) {
      tpos = tt[tpos];
      uint8_t ch = tpos & 0xff;
      tpos >>= 8;
      if (same == 4) { /* nach vier gleichen Bytes: Wiederholungszahl */
        if (op + ch > olen) goto done;
        memset(o + op, prev, ch);
        op += ch;
        same = 0;
        prev = -1;
        continue;
      }
      if (ch == prev) same++;
      else same = 1, prev = ch;
      if (op >= olen) goto done;
      o[op++] = ch;
    }
  }
  ret = (long)op;
done:
  free(tt);
  return ret;
}

/* ---- UDIF ------------------------------------------------------------------------------------- */

static int b64v(int c) {
  if (c >= 'A' && c <= 'Z') return c - 'A';
  if (c >= 'a' && c <= 'z') return c - 'a' + 26;
  if (c >= '0' && c <= '9') return c - '0' + 52;
  return c == '+' ? 62 : c == '/' ? 63 : -1;
}

static size_t b64_decode(const char *s, size_t n, uint8_t *o) {
  size_t k = 0;
  uint32_t acc = 0;
  int bits = 0;
  for (size_t i = 0; i < n; i++) {
    int v = b64v((unsigned char)s[i]);
    if (v < 0) continue;
    acc = acc << 6 | (uint32_t)v;
    bits += 6;
    if (bits >= 8) {
      bits -= 8;
      o[k++] = (uint8_t)(acc >> bits);
    }
  }
  return k;
}

static ssize_t pread_all(int fd, void *buf, size_t n, off_t off) {
  size_t got = 0;
  while (got < n) {
    ssize_t r = pread(fd, (char *)buf + got, n - got, off + (off_t)got);
    if (r < 0 && errno == EINTR) continue;
    if (r <= 0) break;
    got += (size_t)r;
  }
  return (ssize_t)got;
}

static void pwrite_all(int fd, const void *buf, size_t n, off_t off) {
  size_t put = 0;
  while (put < n) {
    ssize_t r = pwrite(fd, (const char *)buf + put, n - put, off + (off_t)put);
    if (r < 0 && errno == EINTR) continue;
    if (r <= 0) fail("Could not write the temporary image: %s", strerror(errno));
    put += (size_t)r;
  }
}

/* Entpackt alle Blöcke des Images in die Datei raw (dünn besetzt); gibt die Größe in Byte zurück */
static uint64_t udif_expand(int fd, int raw) {
  struct stat st;
  if (fstat(fd, &st) || st.st_size < 512) fail("This is not a disk image.");
  uint8_t koly[512];
  if (pread_all(fd, koly, 512, st.st_size - 512) != 512 || memcmp(koly, "koly", 4))
    fail("This is not a UDIF disk image (no “koly” trailer). Compressed or encrypted images of other kinds aren’t supported.");
  uint64_t data_off = be64(koly + 0x18), xml_off = be64(koly + 0xd8), xml_len = be64(koly + 0xe0);
  uint64_t sectors = be64(koly + 0x1ec);
  if (!xml_len || xml_off + xml_len > (uint64_t)st.st_size || xml_len > (256u << 20)) fail("The disk image has no block table.");
  char *xml = malloc(xml_len + 1);
  if (!xml || pread_all(fd, xml, xml_len, (off_t)xml_off) != (ssize_t)xml_len) fail("Could not read the block table.");
  xml[xml_len] = 0;
  char *blkx = strstr(xml, "<key>blkx</key>");
  if (!blkx) fail("The disk image has no block table.");
  char *end = strstr(blkx, "<key>plst</key>");
  if (!end) end = xml + xml_len;
  uint64_t total = 0;
  int nzlib = 0;
  for (char *p = blkx; (p = strstr(p, "<data>")) && p < end;) {
    p += 6;
    char *q = strstr(p, "</data>");
    if (!q) break;
    uint8_t *mish = malloc((size_t)(q - p) + 8);
    if (!mish) fail("Out of memory");
    size_t ml = b64_decode(p, (size_t)(q - p), mish);
    p = q;
    if (ml < 0xcc || memcmp(mish, "mish", 4)) {
      free(mish);
      continue;
    }
    uint64_t first = be64(mish + 8), base = be64(mish + 0x18);
    uint32_t nch = be32(mish + 0xc8);
    if (ml < 0xcc + (size_t)nch * 40) fail("The block table is damaged.");
    for (uint32_t i = 0; i < nch; i++) {
      const uint8_t *c = mish + 0xcc + (size_t)i * 40;
      uint32_t type = be32(c);
      uint64_t sn = be64(c + 8), sc = be64(c + 16), co = be64(c + 24), cl = be64(c + 32);
      if (type == 0xffffffff) break;
      if (type == 0x7ffffffe || !sc) continue;
      uint64_t pos = (first + sn) * 512, len = sc * 512;
      if (pos + len > total) total = pos + len;
      if (type == 0 || type == 2) continue; /* Nullen: Lücke in der Datei */
      if (data_off + base + co + cl > (uint64_t)st.st_size) fail("The disk image is truncated.");
      uint8_t *dst = malloc(len);
      uint8_t *src = malloc(cl ? cl : 1);
      if (!dst || !src) fail("Out of memory");
      if (pread_all(fd, src, cl, (off_t)(data_off + base + co)) != (ssize_t)cl) fail("The disk image is truncated.");
      long got;
      if (type == 1) {
        got = (long)(cl < len ? cl : len);
        memcpy(dst, src, (size_t)got);
      } else if (type == 0x80000005) {
        got = zlib_inflate(src, cl, dst, len);
        nzlib++;
      } else if (type == 0x80000006) {
        got = bz_decompress(src, cl, dst, len);
      } else {
        fail("This disk image uses a compression (%08x) that isn’t supported. Only zlib and bzip2 images (UDZO, UDBZ) and uncompressed ones are.", type);
        got = -1;
      }
      if (got < 0 || (uint64_t)got < len) {
        if (got < 0) fail("A block of the disk image is damaged.");
        memset(dst + got, 0, len - (uint64_t)got);
      }
      pwrite_all(raw, dst, len, (off_t)pos);
      free(dst);
      free(src);
    }
    free(mish);
  }
  (void)nzlib;
  free(xml);
  if (!total) fail("The disk image is empty.");
  if (sectors && total > sectors * 512) total = sectors * 512;
  if (ftruncate(raw, (off_t)total)) fail("Could not size the temporary image: %s", strerror(errno));
  return total;
}

/* ---- LZVN (decmpfs Typ 7 und 8), nach dem Aufbau von Apples lzvn_decode ---------------------------- */

static long lzvn_decode(const uint8_t *src, size_t sn, uint8_t *dst, size_t dn) {
  size_t ip = 0, op = 0, prev = 0;
  while (ip < sn) {
    uint8_t c = src[ip];
    size_t L = 0, M = 0, D = prev, hdr = 1;
    if (c == 0x06) break; /* Ende */
    if (c == 0x0e || c == 0x16) { /* nichts */
      ip++;
      continue;
    }
    if (c >= 0xf0) {
      if (c == 0xf0) {
        if (ip + 1 >= sn) return -1;
        M = (size_t)src[ip + 1] + 16, hdr = 2;
      } else {
        M = c & 15;
      }
    } else if (c >= 0xe0) {
      if (c == 0xe0) {
        if (ip + 1 >= sn) return -1;
        L = (size_t)src[ip + 1] + 16, hdr = 2;
      } else {
        L = c & 15;
      }
    } else if (c >= 0xa0 && c < 0xc0) { /* med_d */
      if (ip + 2 >= sn) return -1;
      L = (c >> 3) & 3;
      M = (((size_t)(c & 7) << 2) | (src[ip + 1] & 3)) + 3;
      D = (size_t)(src[ip + 1] >> 2) | (size_t)src[ip + 2] << 6;
      hdr = 3;
    } else if ((c & 7) == 7) { /* lrg_d */
      if (ip + 2 >= sn) return -1;
      L = c >> 6;
      M = ((c >> 3) & 7) + 3;
      D = (size_t)src[ip + 1] | (size_t)src[ip + 2] << 8;
      hdr = 3;
    } else if ((c & 7) == 6) { /* pre_d: Abstand wie zuvor */
      L = c >> 6;
      M = ((c >> 3) & 7) + 3;
    } else { /* sml_d */
      if (ip + 1 >= sn) return -1;
      L = c >> 6;
      M = ((c >> 3) & 7) + 3;
      D = ((size_t)(c & 7) << 8) | src[ip + 1];
      hdr = 2;
    }
    ip += hdr;
    if (L) {
      if (ip + L > sn || op + L > dn) return -1;
      memcpy(dst + op, src + ip, L);
      ip += L, op += L;
    }
    if (M) {
      if (D == 0 || D > op || op + M > dn) return -1;
      for (size_t i = 0; i < M; i++, op++) dst[op] = dst[op - D];
      prev = D;
    }
  }
  return (long)op;
}

/* ---- HFS+ ------------------------------------------------------------------------------------- */

typedef struct {
  uint64_t size;
  uint32_t blocks;
  struct { uint32_t start, count; } ext[8];
} Fork;

static void fork_parse(const uint8_t *p, Fork *f) {
  f->size = be64(p);
  f->blocks = be32(p + 12);
  for (int i = 0; i < 8; i++) {
    f->ext[i].start = be32(p + 16 + 8 * i);
    f->ext[i].count = be32(p + 20 + 8 * i);
  }
}

typedef struct {
  uint32_t file, start; /* Datei und erster Block der Gruppe */
  uint8_t type;
  struct { uint32_t start, count; } ext[8];
} OvRec;

typedef struct {
  int fd;
  uint64_t off;        /* Beginn des Volumes im Image */
  uint32_t bsize;
  Fork catalog, extents, attrs;
  OvRec *ov;
  size_t nov;
} Vol;

typedef struct { uint32_t start, count; } Ex;

/* Alle Extents einer Fork: erst die acht im Satz, dann der Überlauf aus dem Extents-Baum */
static Ex *fork_extents(Vol *v, const Fork *f, uint32_t fileid, uint8_t type, size_t *n) {
  Ex *e = calloc(8 + 16, sizeof *e);
  size_t cap = 24, k = 0;
  uint64_t total = 0;
  for (int i = 0; i < 8 && f->ext[i].count; i++) {
    e[k++] = (Ex){f->ext[i].start, f->ext[i].count};
    total += f->ext[i].count;
  }
  while (total < f->blocks) {
    OvRec *r = NULL;
    for (size_t i = 0; i < v->nov; i++)
      if (v->ov[i].file == fileid && v->ov[i].type == type && v->ov[i].start == total) r = &v->ov[i];
    if (!r) break;
    for (int i = 0; i < 8 && r->ext[i].count; i++) {
      if (k == cap) e = realloc(e, (cap *= 2) * sizeof *e);
      e[k++] = (Ex){r->ext[i].start, r->ext[i].count};
      total += r->ext[i].count;
    }
  }
  *n = k;
  return e;
}

typedef struct {
  Vol *v;
  Ex *e;
  size_t n;
  uint64_t size;
} Rd;

static void rd_open(Vol *v, Rd *r, const Fork *f, uint32_t fileid, uint8_t type) {
  r->v = v;
  r->e = fork_extents(v, f, fileid, type, &r->n);
  r->size = f->size;
}

static ssize_t rd_read(Rd *r, uint64_t off, void *buf, size_t n) {
  if (off >= r->size) return 0;
  if (n > r->size - off) n = (size_t)(r->size - off);
  size_t done = 0;
  uint64_t bs = r->v->bsize, pos = 0;
  for (size_t i = 0; i < r->n && done < n; i++) {
    uint64_t len = (uint64_t)r->e[i].count * bs;
    if (off + done < pos + len) {
      uint64_t in = off + done - pos, take = len - in;
      if (take > n - done) take = n - done;
      if (pread_all(r->v->fd, (char *)buf + done, (size_t)take, (off_t)(r->v->off + (uint64_t)r->e[i].start * bs + in)) != (ssize_t)take)
        return -1;
      done += (size_t)take;
    }
    pos += len;
  }
  return (ssize_t)done;
}

/* B-Baum: jedes Blatt der Reihe nach, cb(Satz, Länge, Schlüsselende) */
typedef void (*LeafCb)(Vol *v, const uint8_t *key, size_t keylen, const uint8_t *rec, size_t reclen, void *arg);

static void btree_scan(Vol *v, Rd *r, LeafCb cb, void *arg) {
  uint8_t hdr[512];
  if (rd_read(r, 0, hdr, sizeof hdr) < 40) return;
  uint32_t node = be32(hdr + 24), nsize = be16(hdr + 32), total = be32(hdr + 36);
  if (nsize < 512 || nsize > 65536) return;
  uint8_t *nd = malloc(nsize);
  for (uint32_t guard = 0; node && guard <= total && nd; guard++) {
    if (rd_read(r, (uint64_t)node * nsize, nd, nsize) != (ssize_t)nsize) break;
    if (nd[8] != 0xff) break;
    unsigned nrec = be16(nd + 10);
    for (unsigned i = 0; i < nrec; i++) {
      unsigned off = be16(nd + nsize - 2 * (i + 1)), next = be16(nd + nsize - 2 * (i + 2));
      if (off + 2 > nsize) continue;
      unsigned kl = be16(nd + off);
      unsigned doff = (off + 2 + kl + 1) & ~1u;
      unsigned end = i + 1 < nrec ? next : nsize - 2 * (nrec + 1);
      if (doff > nsize || end > nsize || doff > end) continue;
      cb(v, nd + off, kl, nd + doff, end - doff, arg);
    }
    node = be32(nd);
  }
  free(nd);
}

static void ov_cb(Vol *v, const uint8_t *key, size_t kl, const uint8_t *rec, size_t rl, void *arg) {
  (void)arg;
  if (kl < 10 || rl < 64) return;
  v->ov = realloc(v->ov, (v->nov + 1) * sizeof *v->ov);
  OvRec *o = &v->ov[v->nov++];
  o->type = key[2];
  o->file = be32(key + 4);
  o->start = be32(key + 8);
  for (int i = 0; i < 8; i++) o->ext[i].start = be32(rec + 8 * i), o->ext[i].count = be32(rec + 4 + 8 * i);
}

typedef struct {
  uint32_t parent, id, mode, mtime, special;
  char *name;
  uint8_t kind; /* 1 Ordner, 2 Datei */
  uint8_t oflags;
  char ftype[4], creator[4];
  Fork data, rsrc;
  bool compressed;
  uint8_t *decmpfs;
  size_t declen;
} Ent;

typedef struct {
  Ent *e;
  size_t n, cap;
  char volname[256];
} Cat;

/* UTF-16BE (HFS+, zerlegt) nach UTF-8; "/" auf der Platte ist ":" im POSIX-Namen */
static char *utf8_name(const uint8_t *p, unsigned n) {
  char *o = malloc((size_t)n * 3 + 1);
  size_t k = 0;
  for (unsigned i = 0; i < n; i++) {
    uint32_t c = be16(p + 2 * i);
    if (c >= 0xd800 && c < 0xdc00 && i + 1 < n) {
      uint32_t d = be16(p + 2 * (i + 1));
      c = 0x10000 + ((c - 0xd800) << 10) + (d - 0xdc00);
      i++;
    }
    if (c == '/') c = ':';
    if (c < 0x80) o[k++] = (char)c;
    else if (c < 0x800) o[k++] = (char)(0xc0 | c >> 6), o[k++] = (char)(0x80 | (c & 0x3f));
    else if (c < 0x10000) o[k++] = (char)(0xe0 | c >> 12), o[k++] = (char)(0x80 | ((c >> 6) & 0x3f)), o[k++] = (char)(0x80 | (c & 0x3f));
    else o[k++] = (char)(0xf0 | c >> 18), o[k++] = (char)(0x80 | ((c >> 12) & 0x3f)), o[k++] = (char)(0x80 | ((c >> 6) & 0x3f)), o[k++] = (char)(0x80 | (c & 0x3f));
  }
  o[k] = 0;
  return o;
}

#define HFS_EPOCH 2082844800u /* 1904 -> 1970 */

static void cat_cb(Vol *v, const uint8_t *key, size_t kl, const uint8_t *rec, size_t rl, void *arg) {
  (void)v;
  Cat *c = arg;
  if (kl < 6 || rl < 2) return;
  uint32_t parent = be32(key + 2);
  unsigned nl = be16(key + 6);
  if (kl < 6 + 2 * nl) return;
  unsigned type = be16(rec);
  if (type != 1 && type != 2) return;
  if (type == 1 && rl < 88) return;
  if (type == 2 && rl < 248) return;
  if (c->n == c->cap) c->e = realloc(c->e, (c->cap = c->cap ? c->cap * 2 : 1024) * sizeof *c->e);
  Ent *e = &c->e[c->n++];
  memset(e, 0, sizeof *e);
  e->parent = parent;
  e->name = utf8_name(key + 8, nl);
  e->kind = (uint8_t)type;
  e->id = be32(rec + 8);
  uint32_t mt = be32(rec + 16);
  e->mtime = mt > HFS_EPOCH ? mt - HFS_EPOCH : 0;
  e->mode = be16(rec + 42);
  e->special = be32(rec + 44);
  e->oflags = rec[41];
  if (type == 2) {
    memcpy(e->ftype, rec + 48, 4);
    memcpy(e->creator, rec + 52, 4);
    fork_parse(rec + 88, &e->data);
    fork_parse(rec + 168, &e->rsrc);
    e->compressed = (e->oflags & 0x20) != 0; /* UF_COMPRESSED */
  }
  if (parent == 1 && type == 1) snprintf(c->volname, sizeof c->volname, "%s", e->name);
}

static int cmp_parent(const void *a, const void *b) {
  const Ent *x = a, *y = b;
  if (x->parent != y->parent) return x->parent < y->parent ? -1 : 1;
  return strcmp(x->name, y->name);
}

static int cmp_id(const void *a, const void *b) {
  const Ent *x = *(Ent *const *)a, *y = *(Ent *const *)b;
  return x->id < y->id ? -1 : x->id > y->id;
}

/* Attribute: nur com.apple.decmpfs (Inline-Daten) wird gebraucht */
typedef struct {
  Cat *c;
  Ent **byid;
} AttrCtx;

static void attr_cb(Vol *v, const uint8_t *key, size_t kl, const uint8_t *rec, size_t rl, void *arg) {
  (void)v;
  AttrCtx *a = arg;
  if (kl < 12 || rl < 16 || be32(rec) != 0x10) return;
  uint32_t fid = be32(key + 4);
  unsigned nl = be16(key + 12);
  if (kl < 12 + 2 * nl) return;
  char *name = utf8_name(key + 14, nl);
  bool ok = !strcmp(name, "com.apple.decmpfs");
  free(name);
  if (!ok) return;
  uint32_t sz = be32(rec + 12);
  if (sz > rl - 16) return;
  Ent key_e = {.id = fid}, *kp = &key_e;
  Ent **hit = bsearch(&kp, a->byid, a->c->n, sizeof *a->byid, cmp_id);
  if (!hit) return;
  (*hit)->decmpfs = malloc(sz ? sz : 1);
  memcpy((*hit)->decmpfs, rec + 16, sz);
  (*hit)->declen = sz;
}

/* ---- Volume im Image finden ---------------------------------------------------------------------- */

static bool hfs_at(int fd, uint64_t off, uint64_t *real_off) {
  uint8_t b[1024 + 256];
  if (pread_all(fd, b, sizeof b, (off_t)off) != (ssize_t)sizeof b) return false;
  const uint8_t *h = b + 1024;
  if ((h[0] == 'H' && (h[1] == '+' || h[1] == 'X'))) {
    *real_off = off;
    return true;
  }
  if (h[0] == 'B' && h[1] == 'D' && h[124] == 'H' && h[125] == '+') { /* HFS-Hülle um ein HFS+-Volume */
    uint32_t albs = be32(h + 20), alst = be16(h + 28), start = be16(h + 126);
    *real_off = off + (uint64_t)alst * 512 + (uint64_t)start * albs;
    return true;
  }
  return false;
}

static bool find_hfs(int fd, uint64_t size, uint64_t *off) {
  if (hfs_at(fd, 0, off)) return true;
  uint8_t b[512];
  /* Apple-Partitionstabelle: Block 0 "ER", Einträge ab Block 1 */
  if (pread_all(fd, b, 512, 0) == 512 && b[0] == 'E' && b[1] == 'R') {
    uint32_t bsz = be16(b + 2) ? be16(b + 2) : 512;
    for (int i = 1; i < 64; i++) {
      if (pread_all(fd, b, 512, (off_t)i * bsz) != 512 || b[0] != 'P' || b[1] != 'M') break;
      if (!strncmp((char *)b + 48, "Apple_HFS", 9) && hfs_at(fd, (uint64_t)be32(b + 8) * bsz, off)) return true;
    }
  }
  /* GPT, Blockgröße 512 */
  uint8_t hdr[512];
  if (pread_all(fd, hdr, 512, 512) == 512 && !memcmp(hdr, "EFI PART", 8)) {
    uint64_t lba = hdr[72] | (uint64_t)hdr[73] << 8 | (uint64_t)hdr[74] << 16 | (uint64_t)hdr[75] << 24;
    uint32_t n = hdr[80] | hdr[81] << 8, es = hdr[84] | hdr[85] << 8;
    for (uint32_t i = 0; i < n && i < 128 && es >= 128; i++) {
      uint8_t e[128];
      if (pread_all(fd, e, 128, (off_t)(lba * 512 + (uint64_t)i * es)) != 128) break;
      uint64_t first = 0;
      for (int k = 7; k >= 0; k--) first = first << 8 | e[32 + k];
      if (first && first * 512 < size && hfs_at(fd, first * 512, off)) return true;
    }
  }
  return false;
}

/* ---- Auspacken ------------------------------------------------------------------------------------ */

static uint64_t total_bytes, copied_bytes;
static unsigned long nfiles, nskipped;
static char last_skip[300];

static void copy_fork(Vol *v, const Fork *f, uint32_t id, int ofd, const char *path) {
  Rd r;
  rd_open(v, &r, f, id, 0);
  uint8_t *buf = malloc(1 << 20);
  for (uint64_t off = 0; off < r.size;) {
    ssize_t n = rd_read(&r, off, buf, 1 << 20);
    if (n <= 0) {
      snprintf(last_skip, sizeof last_skip, "%s", path);
      nskipped++;
      break;
    }
    if (write(ofd, buf, (size_t)n) != n) fail("Could not write %s: %s", path, strerror(errno));
    off += (uint64_t)n;
    copied_bytes += (uint64_t)n;
  }
  free(buf);
  free(r.e);
}

static void set_times(const char *path, uint32_t mtime) {
  struct timeval tv[2] = {{mtime, 0}, {mtime, 0}};
  utimes(path, tv);
}

static void extract_file(Vol *v, Ent *e, Ent *src, const char *path) {
  unsigned mode = src->mode;
  if ((mode & 0170000) == 0120000 || (!memcmp(src->ftype, "slnk", 4) && !memcmp(src->creator, "rhap", 4))) { /* Symlink */
    Rd r;
    rd_open(v, &r, &src->data, src->id, 0);
    char tgt[1100];
    ssize_t n = r.size < sizeof tgt - 1 ? rd_read(&r, 0, tgt, (size_t)r.size) : -1;
    free(r.e);
    if (n <= 0) return;
    tgt[n] = 0;
    unlink(path);
    if (symlink(tgt, path)) fail("Could not create %s: %s", path, strerror(errno));
    return;
  }
  int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0600);
  if (fd < 0) fail("Could not create %s: %s", path, strerror(errno));
  if (src->compressed && src->decmpfs && src->declen >= 16 && !memcmp(src->decmpfs, "fpmc", 4)) {
    /* decmpfs-Kopf (little endian): "fpmc", Typ, unkomprimierte Größe; Typ 3 = zlib im Attribut, 4 = zlib in der Resource Fork */
    const uint8_t *h = src->decmpfs;
    uint32_t type = h[4] | h[5] << 8 | h[6] << 16 | (uint32_t)h[7] << 24;
    uint64_t usize = 0;
    for (int k = 7; k >= 0; k--) usize = usize << 8 | h[8 + k];
    uint8_t *o = malloc(usize ? usize : 1);
    long got = -1;
    if (type == 7 && src->declen > 16) { /* LZVN im Attribut; erstes Byte 0x06: unkomprimiert dahinter */
      if (h[16] == 0x06) {
        got = (long)(src->declen - 17 < usize ? src->declen - 17 : usize);
        memcpy(o, h + 17, (size_t)got);
      } else {
        got = lzvn_decode(h + 16, src->declen - 16, o, usize);
      }
    } else if (type == 8) { /* LZVN in der Resource Fork: Tabelle der Blockanfänge (little endian) am Anfang */
      Rd rr;
      rd_open(v, &rr, &src->rsrc, src->id, 0xff);
      uint8_t *rf = malloc(rr.size ? rr.size : 1);
      if (rr.size >= 8 && rd_read(&rr, 0, rf, (size_t)rr.size) == (ssize_t)rr.size) {
        uint32_t first = rf[0] | rf[1] << 8 | rf[2] << 16 | (uint32_t)rf[3] << 24, nblk = first / 4 - 1;
        uint64_t op = 0;
        got = 0;
        for (uint32_t b = 0; b < nblk && first <= rr.size; b++) {
          const uint8_t *t = rf + 4 * b;
          uint32_t b0 = t[0] | t[1] << 8 | t[2] << 16 | (uint32_t)t[3] << 24, b1 = t[4] | t[5] << 8 | t[6] << 16 | (uint32_t)t[7] << 24;
          if (b1 < b0 || b1 > rr.size || op >= usize) {
            got = -1;
            break;
          }
          uint64_t want = usize - op < 65536 ? usize - op : 65536;
          long g;
          if (b1 > b0 && rf[b0] == 0x06) { /* unkomprimierter Block */
            g = (long)(b1 - b0 - 1 < want ? b1 - b0 - 1 : want);
            memcpy(o + op, rf + b0 + 1, (size_t)g);
          } else {
            g = lzvn_decode(rf + b0, b1 - b0, o + op, (size_t)want);
          }
          if (g < 0) {
            got = -1;
            break;
          }
          op += (uint64_t)g;
        }
        if (got >= 0) got = (long)op;
      }
      free(rf);
      free(rr.e);
    } else if (type == 3 && src->declen > 16) {
      if (h[16] == 0xff) {
        got = (long)(src->declen - 17 < usize ? src->declen - 17 : usize);
        memcpy(o, h + 17, (size_t)got);
      } else {
        got = zlib_inflate(h + 16, src->declen - 16, o, usize);
      }
    } else if (type == 4) {
      /* Resource Fork: Kopf 0x100 Byte, Tabelle der 64-KiB-Blöcke (little endian) hinter dem Zähler bei 0x104 */
      Rd rr;
      rd_open(v, &rr, &src->rsrc, src->id, 0xff);
      uint8_t *rf = malloc(rr.size ? rr.size : 1);
      if (rr.size > 0x110 && rd_read(&rr, 0, rf, (size_t)rr.size) == (ssize_t)rr.size) {
        uint32_t dataoff = be32(rf), nblk = rf[0x104] | rf[0x105] << 8 | rf[0x106] << 16 | (uint32_t)rf[0x107] << 24;
        (void)dataoff;
        uint64_t op = 0;
        got = 0;
        for (uint32_t b = 0; b < nblk && (uint64_t)0x108 + 8 * (b + 1) <= rr.size; b++) {
          const uint8_t *t = rf + 0x104 + 4 + 8 * b;
          uint32_t boff = t[0] | t[1] << 8 | t[2] << 16 | (uint32_t)t[3] << 24, blen = t[4] | t[5] << 8 | t[6] << 16 | (uint32_t)t[7] << 24;
          if (0x104 + (uint64_t)boff + blen > rr.size) {
            got = -1;
            break;
          }
          const uint8_t *src_b = rf + 0x104 + boff;
          uint64_t want = usize - op < 65536 ? usize - op : 65536;
          long g;
          if (blen && src_b[0] == 0xff) {
            g = (long)(blen - 1 < want ? blen - 1 : want);
            memcpy(o + op, src_b + 1, (size_t)g);
          } else {
            g = zlib_inflate(src_b, blen, o + op, (size_t)want);
          }
          if (g < 0) {
            got = -1;
            break;
          }
          op += (uint64_t)g;
        }
        if (got >= 0) got = (long)op;
      }
      free(rf);
      free(rr.e);
    }
    if (got < 0 || (uint64_t)got != usize) {
      snprintf(last_skip, sizeof last_skip, "%s", path);
      nskipped++;
    } else if (write(fd, o, (size_t)got) != got) {
      fail("Could not write %s: %s", path, strerror(errno));
    }
    copied_bytes += usize;
    free(o);
  } else {
    copy_fork(v, &src->data, src->id, fd, path);
  }
  fchmod(fd, (mode_t)(((mode & 0777) ? (mode & 0777) : 0644) & ~0222));
  close(fd);
  set_times(path, e->mtime);
  nfiles++;
}

typedef struct {
  Vol *v;
  Cat *c;
  Ent **byid;
  size_t *kid_start; /* nicht benutzt */
  char dest[4096];
} Ex2;

static void walk(Ex2 *x, uint32_t parent, char *path, size_t plen, int depth) {
  Cat *c = x->c;
  /* binäre Suche nach dem ersten Eintrag mit diesem Elternteil */
  size_t lo = 0, hi = c->n;
  while (lo < hi) {
    size_t mid = (lo + hi) / 2;
    if (c->e[mid].parent < parent) lo = mid + 1;
    else hi = mid;
  }
  for (size_t i = lo; i < c->n && c->e[i].parent == parent; i++) {
    Ent *e = &c->e[i];
    /* ohne Namen: "\0\0\0\0HFS+ Private Data" mit den Daten der harten Links; das Journal ist Verwaltung */
    if (!strncmp(e->name, ".journal", 8) || !strcmp(e->name, ".HFS+ Private Directory Data\r")) continue;
    if (strlen(e->name) == 0 || !strcmp(e->name, ".") || !strcmp(e->name, "..") || plen + strlen(e->name) + 2 >= sizeof x->dest) continue;
    size_t n = (size_t)snprintf(path + plen, sizeof x->dest - plen, "/%s", e->name);
    if (e->kind == 1) {
      if (mkdir(path, 0700) && errno != EEXIST) fail("Could not create %s: %s", path, strerror(errno));
      walk(x, e->id, path, plen + n, depth + 1);
      chmod(path, (mode_t)(((e->mode & 0777) ? (e->mode & 0777) : 0755) & ~0222));
      set_times(path, e->mtime);
    } else {
      Ent *src = e;
      if (!memcmp(e->ftype, "hlnk", 4) && !memcmp(e->creator, "hfs+", 4)) { /* harter Link: Daten in iNode<N> */
        Ent key_e = {.id = e->special}, *kp = &key_e;
        Ent **hit = bsearch(&kp, x->byid, c->n, sizeof *x->byid, cmp_id);
        if (hit) src = *hit;
      }
      extract_file(x->v, e, src, path);
    }
    path[plen] = 0;
  }
}

static uint64_t free_bytes(const char *dir) {
  struct statfs sf;
  if (statfs(dir, &sf)) return UINT64_MAX;
  return (uint64_t)sf.f_bavail * sf.f_bsize;
}

static const char *volumes_dir(void) {
  const char *v = getenv("CLOKE_VOLUMES");
  return v && *v ? v : "/Volumes";
}

static void cmd_mount(const char *file) {
  int fd = open(file, O_RDONLY);
  if (fd < 0) fail("Could not open %s: %s", file, strerror(errno));
  struct stat fst;
  uint8_t tail[512];
  uint64_t size;
  int raw;
  if (!fstat(fd, &fst) && fst.st_size >= 512 && pread_all(fd, tail, 512, fst.st_size - 512) == 512 && memcmp(tail, "koly", 4)) {
    raw = fd; /* UDRW ohne UDIF-Anhang: das Image ist schon der rohe Datenträger */
    size = (uint64_t)fst.st_size;
  } else {
    const char *tmpdir = getenv("CLOKE_TMP") ? getenv("CLOKE_TMP") : "/var/tmp";
    char rawpath[256];
    snprintf(rawpath, sizeof rawpath, "%s/clokeos-dmg-%d.img", tmpdir, (int)getpid());
    raw = open(rawpath, O_RDWR | O_CREAT | O_TRUNC, 0600);
    if (raw < 0) fail("Could not create the temporary image: %s", strerror(errno));
    unlink(rawpath); /* verschwindet mit dem Prozess */
    out("STEP Reading the disk image");
    size = udif_expand(fd, raw);
  }
  uint64_t hoff;
  if (!find_hfs(raw, size, &hoff)) fail("The disk image contains no HFS+ volume. Other file systems (APFS, FAT, ISO 9660) can’t be opened yet.");
  Vol v = {.fd = raw, .off = hoff};
  uint8_t vh[512];
  if (pread_all(raw, vh, 512, (off_t)(hoff + 1024)) != 512) fail("The HFS+ volume header is unreadable.");
  v.bsize = be32(vh + 40);
  if (v.bsize < 512 || (v.bsize & (v.bsize - 1))) fail("The HFS+ volume header is damaged.");
  fork_parse(vh + 112 + 80, &v.extents); /* Extents-Überlauf: 192 */
  fork_parse(vh + 272, &v.catalog);
  fork_parse(vh + 352, &v.attrs);
  out("STEP Reading the volume");
  Rd rx;
  rd_open(&v, &rx, &v.extents, 3, 0);
  btree_scan(&v, &rx, ov_cb, NULL);
  free(rx.e);
  Cat cat = {0};
  Rd rc;
  rd_open(&v, &rc, &v.catalog, 4, 0);
  btree_scan(&v, &rc, cat_cb, &cat);
  free(rc.e);
  if (!cat.n) fail("The HFS+ catalog is empty or damaged.");
  Ent **byid = malloc(cat.n * sizeof *byid);
  for (size_t i = 0; i < cat.n; i++) byid[i] = &cat.e[i];
  /* byid zeigt in cat.e; erst nach dem Sortieren von cat.e aufbauen */
  qsort(cat.e, cat.n, sizeof *cat.e, cmp_parent);
  for (size_t i = 0; i < cat.n; i++) byid[i] = &cat.e[i];
  qsort(byid, cat.n, sizeof *byid, cmp_id);
  if (v.attrs.blocks) {
    Rd ra;
    rd_open(&v, &ra, &v.attrs, 8, 0);
    AttrCtx ac = {&cat, byid};
    btree_scan(&v, &ra, attr_cb, &ac);
    free(ra.e);
  }
  for (size_t i = 0; i < cat.n; i++)
    if (cat.e[i].kind == 2) total_bytes += cat.e[i].data.size;
  const char *vols = volumes_dir();
  mkdir(vols, 0755);
  if (free_bytes(vols) < total_bytes + (64u << 20)) fail("There is not enough free space to open this disk image (%llu MB needed).", (unsigned long long)(total_bytes >> 20) + 64);

  char name[256], dest[4096];
  snprintf(name, sizeof name, "%s", cat.volname[0] ? cat.volname : "Untitled");
  snprintf(dest, sizeof dest, "%s/%s", vols, name);
  for (int s = 0; mkdir(dest, 0700) != 0;) { /* Name belegt: "Name 1", "Name 2" … */
    if (errno != EEXIST || ++s >= 100) fail("Could not create %s: %s", dest, strerror(errno));
    snprintf(dest, sizeof dest, "%s/%s %d", vols, name, s);
  }
  char marker[4200];
  snprintf(marker, sizeof marker, "%s/" MARK, dest);
  FILE *mf = fopen(marker, "w");
  if (mf) {
    fprintf(mf, "%s\n", file);
    fclose(mf);
  }
  out("STEP Copying the files");
  Ex2 x = {.v = &v, .c = &cat, .byid = byid};
  snprintf(x.dest, sizeof x.dest, "%s", dest);
  char path[4096];
  snprintf(path, sizeof path, "%s", dest);
  walk(&x, 2, path, strlen(path), 0);
  chmod(dest, 0555);
  if (nskipped) fprintf(stderr, "clokeos-dmg: %lu file(s) could not be read, last: %s\n", nskipped, last_skip);
  out("MOUNTED %s", dest);
}

/* ---- Auswerfen ---------------------------------------------------------------------------------- */

static void rm_tree(const char *path) {
  struct stat st;
  if (lstat(path, &st)) return;
  if (S_ISDIR(st.st_mode)) {
    chmod(path, 0700);
    DIR *d = opendir(path);
    struct dirent *de;
    while (d && (de = readdir(d))) {
      if (!strcmp(de->d_name, ".") || !strcmp(de->d_name, "..")) continue;
      char sub[4200];
      snprintf(sub, sizeof sub, "%s/%s", path, de->d_name);
      rm_tree(sub);
    }
    if (d) closedir(d);
    rmdir(path);
  } else {
    unlink(path);
  }
}

static bool is_dmg_volume(const char *path) {
  char m[4200];
  snprintf(m, sizeof m, "%s/" MARK, path);
  return access(m, F_OK) == 0;
}

static void cmd_eject(const char *path) {
  char real[4096];
  if (!realpath(path, real)) fail("No such volume: %s", path);
  const char *vols = volumes_dir();
  char rvols[4096];
  if (!realpath(vols, rvols) || strncmp(real, rvols, strlen(rvols)) || real[strlen(rvols)] != '/' || strchr(real + strlen(rvols) + 1, '/'))
    fail("%s is not a volume.", path);
  if (!is_dmg_volume(real)) fail("%s was not opened from a disk image.", path);
  rm_tree(real);
  out("EJECTED %s", real);
}

static void cmd_list(void) {
  DIR *d = opendir(volumes_dir());
  struct dirent *de;
  while (d && (de = readdir(d))) {
    char p[4200];
    snprintf(p, sizeof p, "%s/%s", volumes_dir(), de->d_name);
    if (de->d_name[0] != '.' && is_dmg_volume(p)) out("VOLUME %s", p);
  }
  if (d) closedir(d);
}

int main(int argc, char **argv) {
  if (argc == 3 && !strcmp(argv[1], "mount")) cmd_mount(argv[2]);
  else if (argc == 3 && !strcmp(argv[1], "eject")) cmd_eject(argv[2]);
  else if (argc == 2 && !strcmp(argv[1], "list")) cmd_list();
  else {
    fprintf(stderr, "usage: clokeos-dmg mount <file.dmg> | eject <volume> | list\n");
    return 2;
  }
  return 0;
}
