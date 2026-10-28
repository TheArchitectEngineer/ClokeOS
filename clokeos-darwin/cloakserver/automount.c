/*
 * clokeos-automount: hängt USB-Sticks und andere Datenträger ein, wie macOS unter /Volumes.
 *
 *   clokeos-automount <uid> <gid>     läuft als root; die Dateien gehören dem angemeldeten Benutzer
 *
 * Der Kernel stürzt ab, wenn man HFS auf ein Gerät mit anderem Dateisystem einhängen lässt. Deshalb
 * erkennt dieses Programm das Dateisystem zuerst an den Kennungen im ersten Sektor und hängt nur ein,
 * was es sicher kennt: FAT12/16/32 (msdos, Lesen und Schreiben) und HFS+/HFSX (nur Lesen). exFAT, NTFS, ext und
 * ISO 9660 erkennt es, hängt sie aber nicht ein (kein Treiber im Kernel oder zu riskant). Der Aufbau der
 * Mount-Argumente ist der von /sbin/mount (mount_msdos, mount_hfs). Alle 2 Sekunden wird /dev neu gelesen; ein
 * verschwundenes Gerät wird wieder ausgehängt.
 */
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <time.h>
#include <sys/types.h>
#include <unistd.h>

#define MAXDEV 32

typedef struct {
  char name[24];     /* "disk2s1" */
  char point[96];    /* "/Volumes/NAME" oder leer */
  int state;         /* 0 neu, 1 eingehängt, 2 nicht unterstützt oder fehlgeschlagen */
} Dev;

static Dev devs[MAXDEV];
static uid_t uid_;
static gid_t gid_;

static int is_disk_node(const char *n) {
  if (strncmp(n, "disk", 4)) return 0;
  const char *p = n + 4;
  if (!isdigit((unsigned char)*p)) return 0;
  while (isdigit((unsigned char)*p)) p++;
  if (!*p) return 1;
  if (*p++ != 's' || !isdigit((unsigned char)*p)) return 0;
  while (isdigit((unsigned char)*p)) p++;
  return !*p;
}

static size_t base_len(const char *n) { /* "disk2s1" -> 5 ("disk2") */
  const char *p = n + 4;
  while (isdigit((unsigned char)*p)) p++;
  return (size_t)(p - n);
}

static const char *probe(const char *dev, char *label, size_t ln) {
  unsigned char b[4096];
  char path[64];
  snprintf(path, sizeof path, "/dev/%s", dev);
  int fd = open(path, O_RDONLY);
  if (fd < 0) return NULL;
  ssize_t n = pread(fd, b, sizeof b, 0);
  close(fd);
  snprintf(label, ln, "Untitled");
  if (n < 2048) return NULL;
  if (b[1024] == 'H' && (b[1025] == '+' || b[1025] == 'X')) return "hfs";
  if (b[1080] == 0x53 && b[1081] == 0xEF) return "ext";
  if (!memcmp(b + 3, "EXFAT   ", 8)) return "exfat";
  if (!memcmp(b + 3, "NTFS    ", 8)) return "ntfs";
  if (b[510] == 0x55 && b[511] == 0xAA && (b[0] == 0xEB || b[0] == 0xE9)) {
    unsigned bps = b[11] | (b[12] << 8), spc = b[13], rsv = b[14] | (b[15] << 8), nfat = b[16];
    int ok = (bps == 512 || bps == 1024 || bps == 2048 || bps == 4096) && spc && !(spc & (spc - 1)) && rsv && (nfat == 1 || nfat == 2);
    if (ok) {
      int f32 = !memcmp(b + 82, "FAT32   ", 8);
      const unsigned char *l = f32 ? b + 71 : b + 43;
      char t[12];
      memcpy(t, l, 11);
      t[11] = 0;
      for (int i = 10; i >= 0 && (t[i] == ' ' || t[i] == 0); i--) t[i] = 0;
      if (t[0] && strcmp(t, "NO NAME")) snprintf(label, ln, "%s", t);
      return "msdos";
    }
  }
  /* ISO 9660 liegt erst bei 32 KB */
  return NULL;
}

static int is_mountpoint_used(const char *path) {
  struct statfs *fs;
  int n = getmntinfo(&fs, MNT_NOWAIT);
  for (int i = 0; i < n; i++)
    if (!strcmp(fs[i].f_mntonname, path)) return 1;
  return 0;
}

static void clean_label(char *s) {
  for (char *p = s; *p; p++)
    if (*p == '/' || *p == ':' || (unsigned char)*p < 32) *p = '_';
}

static int do_mount(const char *type, const char *dev, const char *dir, int rdonly) {
  char fspec[64];
  snprintf(fspec, sizeof fspec, "/dev/%s", dev);
  int flags = MNT_NOSUID | MNT_NODEV | (rdonly ? MNT_RDONLY : 0);
  unsigned char a[0x60];
  memset(a, 0, sizeof a);
  char *sp = fspec;
  if (!strcmp(type, "msdos")) {
    uint32_t u = uid_, g = gid_, magic = 0xe4eff301;
    uint16_t mask = 0775; /* wie bei /sbin/mount: der Kern nimmt die Maske als Zugriffsrechte */
    memcpy(a + 0, &sp, sizeof sp);
    memcpy(a + 8, &u, 4);
    memcpy(a + 12, &g, 4);
    memcpy(a + 16, &mask, 2);
    memcpy(a + 0x18, &magic, 4);
    return mount("msdos", dir, flags, a);
  }
  /* hfs: wie mount_hfs, Besitzer aus dem Dateisystem übernehmen (-1) */
  uint32_t m1 = 0xffffffff;
  uint16_t m16 = 0xffff;
  memcpy(a + 0, &sp, sizeof sp);
  memcpy(a + 8, &m1, 4);
  memcpy(a + 12, &m1, 4);
  memcpy(a + 16, &m16, 2);
  memcpy(a + 0x14, &m1, 4);
  memcpy(a + 0x18, &m1, 4);
  memcpy(a + 0x20, &m1, 4);
  return mount("hfs", dir, flags, a);
}

static Dev *find(const char *name) {
  for (int i = 0; i < MAXDEV; i++)
    if (devs[i].name[0] && !strcmp(devs[i].name, name)) return &devs[i];
  return NULL;
}

static Dev *add(const char *name) {
  for (int i = 0; i < MAXDEV; i++)
    if (!devs[i].name[0]) {
      memset(&devs[i], 0, sizeof devs[i]);
      snprintf(devs[i].name, sizeof devs[i].name, "%s", name);
      return &devs[i];
    }
  return NULL;
}

/* gehört das Gerät (oder seine Platte) zu einem schon eingehängten System-Dateisystem außerhalb von /Volumes? */
static dev_t root_dev(void) {
  struct stat st;
  return stat("/", &st) ? 0 : st.st_dev;
}

static int busy_system(const char *name) {
  /* die Platte mit dem Startvolumen: das Gerät, dessen Nummer die von "/" ist, samt allen Teilen */
  dev_t rd = root_dev();
  size_t bl0 = base_len(name);
  DIR *dd = opendir("/dev");
  struct dirent *de;
  while (rd && dd && (de = readdir(dd))) {
    if (!is_disk_node(de->d_name)) continue;
    char p[64];
    struct stat st;
    snprintf(p, sizeof p, "/dev/%s", de->d_name);
    if (!stat(p, &st) && S_ISBLK(st.st_mode) && st.st_rdev == rd && !strncmp(de->d_name, name, bl0) && base_len(de->d_name) == bl0) {
      closedir(dd);
      return 1;
    }
  }
  if (dd) closedir(dd);
  struct statfs *fs;
  int n = getmntinfo(&fs, MNT_NOWAIT);
  size_t bl = base_len(name);
  for (int i = 0; i < n; i++) {
    const char *from = fs[i].f_mntfromname;
    if (strncmp(from, "/dev/disk", 9)) continue;
    if (!strncmp(fs[i].f_mntonname, "/Volumes/", 9)) continue;
    if (!strncmp(from + 5, name, bl)) return 1; /* dieselbe Platte, z. B. die Startplatte mit der EFI-Partition */
  }
  return 0;
}

/* Das Festplattendienstprogramm (clokeos-installer) schreibt gerade Partitionen: nichts einhängen. Eine liegen
 * gebliebene Sperre (Absturz) gilt nach zehn Minuten nicht mehr. */
static int paused(void) {
  struct stat st;
  return !stat("/var/run/clokeos-diskutil.lock", &st) && time(NULL) - st.st_mtime < 600;
}

static void scan(void) {
  if (paused()) return;
  char names[64][24];
  int nn = 0;
  DIR *d = opendir("/dev");
  if (!d) return;
  struct dirent *e;
  while ((e = readdir(d)) && nn < 64)
    if (is_disk_node(e->d_name)) snprintf(names[nn++], sizeof names[0], "%s", e->d_name);
  closedir(d);

  /* verschwundene Geräte aushängen */
  for (int i = 0; i < MAXDEV; i++) {
    if (!devs[i].name[0]) continue;
    int there = 0;
    for (int k = 0; k < nn; k++)
      if (!strcmp(names[k], devs[i].name)) there = 1;
    if (there) continue;
    if (devs[i].state == 1 && devs[i].point[0]) {
      unmount(devs[i].point, MNT_FORCE);
      rmdir(devs[i].point);
      fprintf(stderr, "automount: %s unmounted (%s)\n", devs[i].point, devs[i].name);
    }
    devs[i].name[0] = 0;
  }

  for (int k = 0; k < nn; k++) {
    Dev *dv = find(names[k]);
    if (dv) continue;
    dv = add(names[k]);
    if (!dv) continue;
    dv->state = 2;
    /* ganze Platte mit Teilgeräten: nur die Teile, nicht die Platte */
    int has_slices = 0;
    size_t bl = base_len(names[k]);
    if (names[k][bl] == 0)
      for (int j = 0; j < nn; j++)
        if (j != k && !strncmp(names[j], names[k], bl) && names[j][bl] == 's') has_slices = 1;
    if (has_slices || busy_system(names[k])) continue;
    char label[64];
    const char *fs = probe(names[k], label, sizeof label);
    if (!fs) {
      fprintf(stderr, "automount: %s: no known file system\n", names[k]);
      continue;
    }
    if (strcmp(fs, "msdos") && strcmp(fs, "hfs")) {
      fprintf(stderr, "automount: %s: %s is not supported\n", names[k], fs);
      continue;
    }
    if (!strcmp(label, "EFI")) continue; /* EFI-Partitionen blendet macOS aus */
    clean_label(label);
    mkdir("/Volumes", 0755);
    char point[96];
    snprintf(point, sizeof point, "/Volumes/%s", label);
    for (int s = 2; s < 20 && (is_mountpoint_used(point) || (access(point, F_OK) == 0 && rmdir(point) && errno != ENOENT)); s++)
      snprintf(point, sizeof point, "/Volumes/%s %d", label, s);
    mkdir(point, 0755);
    if (do_mount(fs, names[k], point, !strcmp(fs, "hfs"))) {
      fprintf(stderr, "automount: %s on %s as %s: %s\n", names[k], point, fs, strerror(errno));
      rmdir(point);
      continue;
    }
    dv->state = 1;
    snprintf(dv->point, sizeof dv->point, "%s", point);
    fprintf(stderr, "automount: %s on %s as %s mounted\n", names[k], point, fs);
  }
}

int main(int argc, char **argv) {
  uid_ = argc > 1 ? (uid_t)atoi(argv[1]) : 501;
  gid_ = argc > 2 ? (gid_t)atoi(argv[2]) : 20;
  setvbuf(stderr, NULL, _IOLBF, 0);
  for (;;) {
    scan();
    sleep(2);
  }
}
