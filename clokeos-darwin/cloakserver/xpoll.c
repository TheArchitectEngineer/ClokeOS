/* libclokeos-xpoll: Xorg ohne Dauerlast auf PureDarwin.
 *
 * PureDarwins HID-Treiber (/dev/usb_hid_kbd, /dev/usb_hid_mouse) können kein poll und kein select:
 * Beide kehren sofort zurück, mit POLLNVAL, auch wenn keine Eingabe da ist. Xorg wartet in poll()
 * auf Clients und Eingabegeräte und kreist deshalb mit voller Last; auf der einen CPU der VM bleibt
 * den Programmen nur die Hälfte (zsh-Schleife mit 2 Mio. Durchläufen: 6,8 statt 3,5 s auf arm64).
 *
 * cloakserver-xorg lädt diese Bibliothek mit DYLD_INSERT_LIBRARIES in Xorg und leitet poll() um
 * (dyld-Interposing). Ein Zeichengerät, das poll mit POLLNVAL beantwortet, merkt sie sich. Bei den
 * folgenden Aufrufen bleibt es aus dem echten poll heraus, die Wartezeit wird auf 10 ms begrenzt,
 * und danach meldet es POLLIN: Xorgs Treiber liest dann, bekommt Ereignisse oder EAGAIN und wartet
 * im nächsten poll wieder. Eingaben werden so mit 100 Hz abgefragt; alles andere wie bisher.
 */
#include <errno.h>
#include <poll.h>
#include <sys/stat.h>

#define SLICE_MS 10
#define MAXFD 1024

static dev_t nopoll[MAXFD]; /* Gerätenummer + 1 eines Geräts ohne poll, sonst 0 */

/* noch dasselbe Gerät? (das fd könnte inzwischen geschlossen und neu vergeben sein) */
static int is_nopoll(int fd) {
  if (fd < 0 || fd >= MAXFD || !nopoll[fd]) return 0;
  struct stat st;
  if (fstat(fd, &st) == 0 && S_ISCHR(st.st_mode) && st.st_rdev + 1 == nopoll[fd]) return 1;
  nopoll[fd] = 0;
  return 0;
}

static int clokeos_poll(struct pollfd *fds, nfds_t n, int timeout) {
  int saved[64], held = 0;
  for (nfds_t i = 0; i < n && held < 64; i++) {
    if (is_nopoll(fds[i].fd) && (fds[i].events & POLLIN)) {
      saved[held++] = fds[i].fd;
      fds[i].fd = -1; /* poll übergeht negative fds */
    }
  }
  int t = timeout;
  if (held && (t < 0 || t > SLICE_MS)) t = SLICE_MS;
  int r = poll(fds, n, t);
  int err = errno;
  if (held) {
    int k = 0;
    for (nfds_t i = 0; i < n && k < held; i++) {
      if (fds[i].fd == -1) {
        fds[i].fd = saved[k++];
        fds[i].revents = POLLIN;
        if (r >= 0) r++;
      }
    }
  }
  if (r > 0) {
    for (nfds_t i = 0; i < n; i++) {
      if (!(fds[i].revents & POLLNVAL) || fds[i].fd < 0 || fds[i].fd >= MAXFD) continue;
      struct stat st;
      if (fstat(fds[i].fd, &st) == 0 && S_ISCHR(st.st_mode)) {
        nopoll[fds[i].fd] = st.st_rdev + 1;
        fds[i].revents = POLLIN; /* lesen lassen; ab dem nächsten Aufruf mit Wartezeit */
      }
    }
  }
  errno = err;
  return r;
}

__attribute__((used, section("__DATA,__interpose"))) static const struct {
  const void *replacement, *replacee;
} interpose_poll = {(const void *)clokeos_poll, (const void *)poll};
