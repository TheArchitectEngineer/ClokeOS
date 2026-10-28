/* libclokeos_compat, Teil 2: CommonCrypto-Digests und kleine libSystem-Funktionen, die Homebrews
 * Portable Ruby und Bottles (Big Sur) importieren, PureDarwins libSystem 20.5 aber nicht hat.
 * Nicht Vorhandenes wird nachgebaut, was sich nicht sinnvoll nachbauen lässt (libunwind,
 * ucontext, Mach-Ausnahme-Ports), meldet einen Fehler wie auf einem System ohne die Funktion. */
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <stdint.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

/* ---- CommonCrypto: MD5, SHA-1, SHA-384, SHA-512 ----
 * Die Kontexte liegen beim Aufrufer in der Größe aus Apples <CommonCrypto/CommonDigest.h>
 * (MD5 92, SHA1 96, SHA512 216 Bytes); die eigenen Zustände sind kleiner. */
#define ROL32(x, n) (((x) << (n)) | ((x) >> (32 - (n))))
#define ROR64(x, n) (((x) >> (n)) | ((x) << (64 - (n))))

typedef struct { uint32_t h[4]; uint64_t n; uint8_t buf[64]; } md5_ctx;
typedef struct { uint32_t h[5]; uint64_t n; uint8_t buf[64]; } sha1_ctx;
typedef struct { uint64_t h[8]; uint64_t n[2]; uint8_t buf[128]; } sha512_ctx;

static const uint32_t md5_k[64] = {
  0xd76aa478, 0xe8c7b756, 0x242070db, 0xc1bdceee, 0xf57c0faf, 0x4787c62a, 0xa8304613, 0xfd469501,
  0x698098d8, 0x8b44f7af, 0xffff5bb1, 0x895cd7be, 0x6b901122, 0xfd987193, 0xa679438e, 0x49b40821,
  0xf61e2562, 0xc040b340, 0x265e5a51, 0xe9b6c7aa, 0xd62f105d, 0x02441453, 0xd8a1e681, 0xe7d3fbc8,
  0x21e1cde6, 0xc33707d6, 0xf4d50d87, 0x455a14ed, 0xa9e3e905, 0xfcefa3f8, 0x676f02d9, 0x8d2a4c8a,
  0xfffa3942, 0x8771f681, 0x6d9d6122, 0xfde5380c, 0xa4beea44, 0x4bdecfa9, 0xf6bb4b60, 0xbebfbc70,
  0x289b7ec6, 0xeaa127fa, 0xd4ef3085, 0x04881d05, 0xd9d4d039, 0xe6db99e5, 0x1fa27cf8, 0xc4ac5665,
  0xf4292244, 0x432aff97, 0xab9423a7, 0xfc93a039, 0x655b59c3, 0x8f0ccc92, 0xffeff47d, 0x85845dd1,
  0x6fa87e4f, 0xfe2ce6e0, 0xa3014314, 0x4e0811a1, 0xf7537e82, 0xbd3af235, 0x2ad7d2bb, 0xeb86d391};
static const uint8_t md5_s[64] = {7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22,
  5, 9, 14, 20, 5, 9, 14, 20, 5, 9, 14, 20, 5, 9, 14, 20, 4, 11, 16, 23, 4, 11, 16, 23,
  4, 11, 16, 23, 4, 11, 16, 23, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21};

static void md5_block(md5_ctx *c, const uint8_t *p) {
  uint32_t m[16], a = c->h[0], b = c->h[1], cc = c->h[2], d = c->h[3];
  for (int i = 0; i < 16; i++) m[i] = p[4 * i] | p[4 * i + 1] << 8 | p[4 * i + 2] << 16 | (uint32_t)p[4 * i + 3] << 24;
  for (int i = 0; i < 64; i++) {
    uint32_t f; int g;
    if (i < 16) { f = (b & cc) | (~b & d); g = i; }
    else if (i < 32) { f = (d & b) | (~d & cc); g = (5 * i + 1) & 15; }
    else if (i < 48) { f = b ^ cc ^ d; g = (3 * i + 5) & 15; }
    else { f = cc ^ (b | ~d); g = (7 * i) & 15; }
    uint32_t t = d; d = cc; cc = b;
    b = b + ROL32(a + f + md5_k[i] + m[g], md5_s[i]);
    a = t;
  }
  c->h[0] += a; c->h[1] += b; c->h[2] += cc; c->h[3] += d;
}

int CC_MD5_Init(void *ctx) {
  md5_ctx *c = ctx;
  c->h[0] = 0x67452301; c->h[1] = 0xefcdab89; c->h[2] = 0x98badcfe; c->h[3] = 0x10325476; c->n = 0;
  return 1;
}
int CC_MD5_Update(void *ctx, const void *data, uint32_t len) {
  md5_ctx *c = ctx; const uint8_t *p = data;
  while (len) {
    uint32_t off = c->n & 63, k = 64 - off < len ? 64 - off : len;
    memcpy(c->buf + off, p, k); c->n += k; p += k; len -= k;
    if (((c->n) & 63) == 0) md5_block(c, c->buf);
  }
  return 1;
}
int CC_MD5_Final(unsigned char *md, void *ctx) {
  md5_ctx *c = ctx; uint64_t bits = c->n * 8; uint8_t pad = 0x80, z = 0, lenb[8];
  CC_MD5_Update(c, &pad, 1);
  while ((c->n & 63) != 56) CC_MD5_Update(c, &z, 1);
  for (int i = 0; i < 8; i++) lenb[i] = bits >> (8 * i);
  CC_MD5_Update(c, lenb, 8);
  for (int i = 0; i < 16; i++) md[i] = c->h[i / 4] >> (8 * (i % 4));
  return 1;
}

static void sha1_block(sha1_ctx *c, const uint8_t *p) {
  uint32_t w[80], a = c->h[0], b = c->h[1], cc = c->h[2], d = c->h[3], e = c->h[4];
  for (int i = 0; i < 16; i++) w[i] = (uint32_t)p[4 * i] << 24 | p[4 * i + 1] << 16 | p[4 * i + 2] << 8 | p[4 * i + 3];
  for (int i = 16; i < 80; i++) w[i] = ROL32(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
  for (int i = 0; i < 80; i++) {
    uint32_t f, k;
    if (i < 20) { f = (b & cc) | (~b & d); k = 0x5a827999; }
    else if (i < 40) { f = b ^ cc ^ d; k = 0x6ed9eba1; }
    else if (i < 60) { f = (b & cc) | (b & d) | (cc & d); k = 0x8f1bbcdc; }
    else { f = b ^ cc ^ d; k = 0xca62c1d6; }
    uint32_t t = ROL32(a, 5) + f + e + k + w[i];
    e = d; d = cc; cc = ROL32(b, 30); b = a; a = t;
  }
  c->h[0] += a; c->h[1] += b; c->h[2] += cc; c->h[3] += d; c->h[4] += e;
}
int CC_SHA1_Init(void *ctx) {
  sha1_ctx *c = ctx;
  c->h[0] = 0x67452301; c->h[1] = 0xefcdab89; c->h[2] = 0x98badcfe; c->h[3] = 0x10325476; c->h[4] = 0xc3d2e1f0;
  c->n = 0;
  return 1;
}
int CC_SHA1_Update(void *ctx, const void *data, uint32_t len) {
  sha1_ctx *c = ctx; const uint8_t *p = data;
  while (len) {
    uint32_t off = c->n & 63, k = 64 - off < len ? 64 - off : len;
    memcpy(c->buf + off, p, k); c->n += k; p += k; len -= k;
    if ((c->n & 63) == 0) sha1_block(c, c->buf);
  }
  return 1;
}
int CC_SHA1_Final(unsigned char *md, void *ctx) {
  sha1_ctx *c = ctx; uint64_t bits = c->n * 8; uint8_t pad = 0x80, z = 0, lenb[8];
  CC_SHA1_Update(c, &pad, 1);
  while ((c->n & 63) != 56) CC_SHA1_Update(c, &z, 1);
  for (int i = 0; i < 8; i++) lenb[i] = bits >> (56 - 8 * i);
  CC_SHA1_Update(c, lenb, 8);
  for (int i = 0; i < 20; i++) md[i] = c->h[i / 4] >> (24 - 8 * (i % 4));
  return 1;
}

static const uint64_t sha512_k[80] = {
  0x428a2f98d728ae22ULL, 0x7137449123ef65cdULL, 0xb5c0fbcfec4d3b2fULL, 0xe9b5dba58189dbbcULL, 0x3956c25bf348b538ULL,
  0x59f111f1b605d019ULL, 0x923f82a4af194f9bULL, 0xab1c5ed5da6d8118ULL, 0xd807aa98a3030242ULL, 0x12835b0145706fbeULL,
  0x243185be4ee4b28cULL, 0x550c7dc3d5ffb4e2ULL, 0x72be5d74f27b896fULL, 0x80deb1fe3b1696b1ULL, 0x9bdc06a725c71235ULL,
  0xc19bf174cf692694ULL, 0xe49b69c19ef14ad2ULL, 0xefbe4786384f25e3ULL, 0x0fc19dc68b8cd5b5ULL, 0x240ca1cc77ac9c65ULL,
  0x2de92c6f592b0275ULL, 0x4a7484aa6ea6e483ULL, 0x5cb0a9dcbd41fbd4ULL, 0x76f988da831153b5ULL, 0x983e5152ee66dfabULL,
  0xa831c66d2db43210ULL, 0xb00327c898fb213fULL, 0xbf597fc7beef0ee4ULL, 0xc6e00bf33da88fc2ULL, 0xd5a79147930aa725ULL,
  0x06ca6351e003826fULL, 0x142929670a0e6e70ULL, 0x27b70a8546d22ffcULL, 0x2e1b21385c26c926ULL, 0x4d2c6dfc5ac42aedULL,
  0x53380d139d95b3dfULL, 0x650a73548baf63deULL, 0x766a0abb3c77b2a8ULL, 0x81c2c92e47edaee6ULL, 0x92722c851482353bULL,
  0xa2bfe8a14cf10364ULL, 0xa81a664bbc423001ULL, 0xc24b8b70d0f89791ULL, 0xc76c51a30654be30ULL, 0xd192e819d6ef5218ULL,
  0xd69906245565a910ULL, 0xf40e35855771202aULL, 0x106aa07032bbd1b8ULL, 0x19a4c116b8d2d0c8ULL, 0x1e376c085141ab53ULL,
  0x2748774cdf8eeb99ULL, 0x34b0bcb5e19b48a8ULL, 0x391c0cb3c5c95a63ULL, 0x4ed8aa4ae3418acbULL, 0x5b9cca4f7763e373ULL,
  0x682e6ff3d6b2b8a3ULL, 0x748f82ee5defb2fcULL, 0x78a5636f43172f60ULL, 0x84c87814a1f0ab72ULL, 0x8cc702081a6439ecULL,
  0x90befffa23631e28ULL, 0xa4506cebde82bde9ULL, 0xbef9a3f7b2c67915ULL, 0xc67178f2e372532bULL, 0xca273eceea26619cULL,
  0xd186b8c721c0c207ULL, 0xeada7dd6cde0eb1eULL, 0xf57d4f7fee6ed178ULL, 0x06f067aa72176fbaULL, 0x0a637dc5a2c898a6ULL,
  0x113f9804bef90daeULL, 0x1b710b35131c471bULL, 0x28db77f523047d84ULL, 0x32caab7b40c72493ULL, 0x3c9ebe0a15c9bebcULL,
  0x431d67c49c100d4cULL, 0x4cc5d4becb3e42b6ULL, 0x597f299cfc657e2aULL, 0x5fcb6fab3ad6faecULL, 0x6c44198c4a475817ULL};

static void sha512_block(sha512_ctx *c, const uint8_t *p) {
  uint64_t w[80], v[8];
  for (int i = 0; i < 16; i++) {
    w[i] = 0;
    for (int j = 0; j < 8; j++) w[i] = w[i] << 8 | p[8 * i + j];
  }
  for (int i = 16; i < 80; i++) {
    uint64_t s0 = ROR64(w[i - 15], 1) ^ ROR64(w[i - 15], 8) ^ (w[i - 15] >> 7);
    uint64_t s1 = ROR64(w[i - 2], 19) ^ ROR64(w[i - 2], 61) ^ (w[i - 2] >> 6);
    w[i] = w[i - 16] + s0 + w[i - 7] + s1;
  }
  memcpy(v, c->h, sizeof v);
  for (int i = 0; i < 80; i++) {
    uint64_t S1 = ROR64(v[4], 14) ^ ROR64(v[4], 18) ^ ROR64(v[4], 41);
    uint64_t ch = (v[4] & v[5]) ^ (~v[4] & v[6]);
    uint64_t t1 = v[7] + S1 + ch + sha512_k[i] + w[i];
    uint64_t S0 = ROR64(v[0], 28) ^ ROR64(v[0], 34) ^ ROR64(v[0], 39);
    uint64_t mj = (v[0] & v[1]) ^ (v[0] & v[2]) ^ (v[1] & v[2]);
    uint64_t t2 = S0 + mj;
    v[7] = v[6]; v[6] = v[5]; v[5] = v[4]; v[4] = v[3] + t1; v[3] = v[2]; v[2] = v[1]; v[1] = v[0]; v[0] = t1 + t2;
  }
  for (int i = 0; i < 8; i++) c->h[i] += v[i];
}
static int sha512_update(sha512_ctx *c, const void *data, uint32_t len) {
  const uint8_t *p = data;
  while (len) {
    uint32_t off = c->n[0] & 127, k = 128 - off < len ? 128 - off : len;
    memcpy(c->buf + off, p, k); c->n[0] += k; p += k; len -= k;
    if ((c->n[0] & 127) == 0) sha512_block(c, c->buf);
  }
  return 1;
}
static int sha512_final(sha512_ctx *c, unsigned char *md, int outlen) {
  uint64_t bits = c->n[0] * 8; uint8_t pad = 0x80, z = 0, lenb[16] = {0};
  sha512_update(c, &pad, 1);
  while ((c->n[0] & 127) != 112) sha512_update(c, &z, 1);
  for (int i = 0; i < 8; i++) lenb[8 + i] = bits >> (56 - 8 * i);
  sha512_update(c, lenb, 16);
  for (int i = 0; i < outlen; i++) md[i] = c->h[i / 8] >> (56 - 8 * (i % 8));
  return 1;
}
int CC_SHA512_Init(void *ctx) {
  static const uint64_t iv[8] = {0x6a09e667f3bcc908ULL, 0xbb67ae8584caa73bULL, 0x3c6ef372fe94f82bULL, 0xa54ff53a5f1d36f1ULL,
                                 0x510e527fade682d1ULL, 0x9b05688c2b3e6c1fULL, 0x1f83d9abfb41bd6bULL, 0x5be0cd19137e2179ULL};
  sha512_ctx *c = ctx; memcpy(c->h, iv, sizeof iv); c->n[0] = c->n[1] = 0;
  return 1;
}
int CC_SHA512_Update(void *ctx, const void *data, uint32_t len) { return sha512_update(ctx, data, len); }
int CC_SHA512_Final(unsigned char *md, void *ctx) { return sha512_final(ctx, md, 64); }
int CC_SHA384_Init(void *ctx) {
  static const uint64_t iv[8] = {0xcbbb9d5dc1059ed8ULL, 0x629a292a367cd507ULL, 0x9159015a3070dd17ULL, 0x152fecd8f70e5939ULL,
                                 0x67332667ffc00b31ULL, 0x8eb44a8768581511ULL, 0xdb0c2e0d64f98fa7ULL, 0x47b5481dbefa4fa4ULL};
  sha512_ctx *c = ctx; memcpy(c->h, iv, sizeof iv); c->n[0] = c->n[1] = 0;
  return 1;
}
int CC_SHA384_Update(void *ctx, const void *data, uint32_t len) { return sha512_update(ctx, data, len); }
int CC_SHA384_Final(unsigned char *md, void *ctx) { return sha512_final(ctx, md, 48); }

/* ---- kleine libSystem-Funktionen ---- */
int clock_getres(clockid_t id, struct timespec *res) {
  if (id != CLOCK_REALTIME && id != CLOCK_MONOTONIC && id != CLOCK_MONOTONIC_RAW && id != CLOCK_PROCESS_CPUTIME_ID &&
      id != CLOCK_THREAD_CPUTIME_ID && id != CLOCK_UPTIME_RAW) {
    errno = EINVAL;
    return -1;
  }
  if (res) { res->tv_sec = 0; res->tv_nsec = 1000; }   /* die Uhren laufen in Mikrosekunden (gettimeofday) bis Nanosekunden */
  return 0;
}

int lchmod(const char *path, mode_t mode) {
  struct stat st;
  if (lstat(path, &st) != 0) return -1;
  if (S_ISLNK(st.st_mode)) { errno = ENOTSUP; return -1; }
  return chmod(path, mode);
}

double lgamma_r(double x, int *sign) {
  *sign = (x < 0 && fmod(floor(x), 2.0) != 0) ? -1 : 1;   /* Vorzeichen von Gamma(x) */
  return lgamma(x);
}

int setruid(uid_t uid) { return setreuid(uid, (uid_t)-1); }
int setrgid(gid_t gid) { return setregid(gid, (gid_t)-1); }

void seekdir(DIR *d, long loc) { (void)d; (void)loc; }
long telldir(DIR *d) { (void)d; return 0; }

int fgetattrlist(int fd, void *al, void *buf, size_t size, unsigned int opts) {
  (void)fd; (void)al; (void)buf; (void)size; (void)opts;
  errno = ENOTSUP;
  return -1;
}

int proc_regionfilename(int pid, uint64_t addr, void *buf, uint32_t size) {
  (void)pid; (void)addr; (void)buf; (void)size;
  errno = ENOTSUP;
  return 0;
}

/* ucontext: Ruby nutzt auf arm64 und x86_64 eigene Coroutinen-Assembler, die Symbole sind nur gebunden */
int getcontext(void *u) { (void)u; errno = ENOSYS; return -1; }
int setcontext(const void *u) { (void)u; errno = ENOSYS; return -1; }
void makecontext(void *u, void (*fn)(void), int argc, ...) { (void)u; (void)fn; (void)argc; }

/* libunwind: ohne sie gibt Ruby bei einem Absturz keine native Rückverfolgung aus */
int unw_getcontext(void *c) { (void)c; return -1; }
int unw_init_local(void *c, void *u) { (void)c; (void)u; return -6540; }   /* UNW_EUNSPEC */
int unw_step(void *c) { (void)c; return -6540; }
int unw_get_reg(void *c, int r, uint64_t *v) { (void)c; (void)r; (void)v; return -6540; }

/* Mach: Ausnahme-Ports und Speicherbereiche, KERN_FAILURE */
int task_swap_exception_ports(unsigned task, unsigned mask, unsigned new_port, int behavior, int flavor, unsigned *masks,
                              unsigned *count, unsigned *ports, int *behaviors, int *flavors) {
  (void)task; (void)mask; (void)new_port; (void)behavior; (void)flavor; (void)masks; (void)count; (void)ports;
  (void)behaviors; (void)flavors;
  return 5;
}
int vm_region_recurse_64(unsigned task, uintptr_t *addr, uintptr_t *size, uint32_t *depth, void *info, uint32_t *count) {
  (void)task; (void)addr; (void)size; (void)depth; (void)info; (void)count;
  return 5;
}

/* git-remote-http (PureDarwin) ist flach gebunden und ruft libcurls Proxy-Abfrage über SystemConfiguration
 * auf, ohne die Bibliothek zu laden: ohne diese beiden Funktionen bricht jedes git clone über https ab.
 * Zweistufig gebundene Programme nehmen weiter die echten aus SystemConfiguration und CoreFoundation. */
void *SCDynamicStoreCopyProxies(void *store) { (void)store; return 0; }
void CFRelease(const void *cf) { (void)cf; }
