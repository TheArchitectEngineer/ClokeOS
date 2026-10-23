/* Wiederherstellung (Recovery) wie bei macOS. Wird in cloakserver.c eingebunden (#include) und
 * nutzt dessen Zeichenhelfer und Fenster.
 *
 * Liegt /etc/clokeos-recovery im System (recovery/build.sh legt es in die Wiederherstellung und das
 * Installationsmedium), zeigt CloakServer statt Anmeldung, Dock und Schreibtisch das Fenster
 * "Wiederherstellung": ClokeOS installieren, Online-Hilfe, Festplattendienstprogramm, Terminal.
 * Installiert wird mit clokeos-installer (recovery/installer.c), der das aktuelle Release aus dem
 * Internet lädt; sein Fortschritt kommt zeilenweise über eine Leitung herein (rec_read).
 *
 * Im normalen System gibt es dafür "Neustart in Wiederherstellung …" im ClokeOS-Menü: Der Installer
 * stellt die Boot-Argumente der EFI-Partition einmal auf die Wiederherstellung um. */

#define INSTALLER "/usr/libexec/clokeos-installer"
#define RECOVERY_MARK "/etc/clokeos-recovery"

#define REC_ONLINE "https://github.com/Karl-Lauterbach24/ClokeOS/releases/latest/download"
static char rec_releases[512] = REC_ONLINE;
/* Das USB-Installationsmedium (recovery/usb.sh) bringt die Abbilder mit: dann ohne Internet */
#define REC_LOCAL_DIR "/Library/ClokeOS/Install"

typedef struct {
  char name[16], label[64];
  unsigned long long bytes;
  bool root, installer, clokeos;
  int nparts;
  struct { char name[48]; unsigned long long bytes; } part[16];
} RecDisk;
static RecDisk rdisks[16];
static int nrdisks;

enum { RP_CHOOSE, RP_INTRO, RP_DISK, RP_CONFIRM, RP_RUN, RP_DONE, RP_ERROR };
static int rp, rsel, rdsel = -1;
static char rstep[200], rerror[400], rversion[32];
static unsigned long long rdone, rtotal;
static double rt0;
static int rec_fd = -1;
static pid_t rec_pid;
static char rec_buf[4096];
static size_t rec_len;
static Client *rwin, *dwin; /* Fenster der Wiederherstellung und des Festplattendienstprogramms */
static int dsel = -1;
static bool dconfirm;
static char dmsg[200];

#define RW 640
#define RH 440

static void rec_init(void) {
  FILE *f = fopen(RECOVERY_MARK, "r");
  if (!f) return;
  recovery = true;
  char line[640];
  while (fgets(line, sizeof line, f)) {
    if (strncmp(line, "RELEASES=\"", 10)) continue;
    char *v = line + 10, *e = strchr(v, '"');
    if (e) *e = 0;
    snprintf(rec_releases, sizeof rec_releases, "%s", v);
  }
  fclose(f);
  struct utsname u;
  char manifest[256];
  if (!uname(&u)) {
    snprintf(manifest, sizeof manifest, REC_LOCAL_DIR "/clokeos-%s.txt", u.machine);
    if (!access(manifest, R_OK)) {
      snprintf(rec_releases, sizeof rec_releases, "file://" REC_LOCAL_DIR);
      return;
    }
  }
  /* Offline-USB-Stick, auf dem nur die schlanke RAM-Disk passte: die Abbilder liegen auf dem Stick
   * selbst, der Installer liest sie von dort ("stick:", recovery/installer.c) */
  child_wait_begin();
  FILE *p = popen(INSTALLER " stick 2>/dev/null", "r");
  if (p) {
    char out[128] = "";
    if (fgets(out, sizeof out, p) && !strncmp(out, "STICK ", 6)) snprintf(rec_releases, sizeof rec_releases, "stick:");
    pclose(p);
  }
  child_wait_end();
}

static bool rec_offline(void) { return !strncmp(rec_releases, "file:", 5) || !strcmp(rec_releases, "stick:"); }

/* 8 589 934 592 -> "8.6 GB" (Deutsch "8,6 GB") */
static void fmt_size(char *out, size_t n, unsigned long long b) {
  double v = b / 1e9;
  const char *unit = "GB";
  if (v < 1) v = b / 1e6, unit = "MB";
  snprintf(out, n, v < 10 ? "%.1f %s" : "%.0f %s", v, unit);
  if (!strcmp(l10n_lang(), "de"))
    for (char *p = out; *p; p++)
      if (*p == '.') *p = ',';
}

static void rec_load_disks(void) {
  nrdisks = 0;
  child_wait_begin();
  FILE *p = popen(INSTALLER " disks 2>/dev/null", "r");
  if (!p) {
    child_wait_end();
    return;
  }
  char line[256];
  while (fgets(line, sizeof line, p)) {
    line[strcspn(line, "\n")] = 0;
    char name[16], flags[8];
    unsigned long long bytes;
    int off = 0;
    if (sscanf(line, "DISK %15s %llu %7s %n", name, &bytes, flags, &off) == 3 && nrdisks < 16) {
      RecDisk *d = &rdisks[nrdisks++];
      memset(d, 0, sizeof *d);
      snprintf(d->name, sizeof d->name, "%s", name);
      snprintf(d->label, sizeof d->label, "%s", line + off);
      d->bytes = bytes;
      d->root = strchr(flags, 'r');
      d->installer = strchr(flags, 'i');
      d->clokeos = strchr(flags, 'c');
    } else if (sscanf(line, "PART %15s %*d %llu %n", name, &bytes, &off) == 2 && nrdisks &&
               !strcmp(name, rdisks[nrdisks - 1].name) && rdisks[nrdisks - 1].nparts < 16) {
      RecDisk *d = &rdisks[nrdisks - 1];
      snprintf(d->part[d->nparts].name, sizeof d->part[0].name, "%s", line + off);
      d->part[d->nparts++].bytes = bytes;
    }
  }
  pclose(p);
  child_wait_end();
}

/* Absatz mit Zeilenumbruch, gibt die Höhe zurück */
static int para(cairo_t *cr, double x, double y, int w, const char *s, double px, RGBA col, bool center) {
  PangoLayout *l = pango_cairo_create_layout(cr);
  PangoFontDescription *f = pango_font_description_from_string("DejaVu Sans");
  pango_font_description_set_absolute_size(f, px * PANGO_SCALE);
  pango_layout_set_font_description(l, f);
  pango_layout_set_width(l, w * PANGO_SCALE);
  pango_layout_set_wrap(l, PANGO_WRAP_WORD);
  pango_layout_set_alignment(l, center ? PANGO_ALIGN_CENTER : PANGO_ALIGN_LEFT);
  pango_layout_set_spacing(l, 2 * PANGO_SCALE);
  pango_layout_set_text(l, s, -1);
  int tw, th;
  pango_layout_get_pixel_size(l, &tw, &th);
  cairo_move_to(cr, x, y);
  set(cr, col);
  pango_cairo_show_layout(cr, l);
  pango_font_description_free(f);
  g_object_unref(l);
  return th;
}

/* Knöpfe unten rechts, wie in den Rückfragen von CloakServer */
typedef struct {
  const char *label;
  int action;
  bool primary, enabled, danger;
  double x, y, w;
} RecBtn;

static int layout_btns(Client *c, RecBtn *b, int n) {
  double x = c->w - 20;
  for (int i = n - 1; i >= 0; i--) {
    b[i].w = text_width(b[i].label, 12.5, false) + 32;
    x -= b[i].w;
    b[i].x = x;
    b[i].y = c->h - 46;
    x -= 10;
  }
  return n;
}

static void paint_btns(cairo_t *cr, const RecBtn *b, int n) {
  for (int i = 0; i < n; i++) {
    rrect(cr, b[i].x, b[i].y, b[i].w, 28, 14);
    if (b[i].primary) {
      RGBA col = b[i].danger ? (RGBA){.86, .24, .26, 1} : ACCENT;
      if (!b[i].enabled) col.a = .35;
      set(cr, col);
      cairo_fill(cr);
      text(cr, b[i].x + b[i].w / 2, b[i].y + 14, b[i].label, 12.5, false, (RGBA){1, 1, 1, b[i].enabled ? 1 : .7}, 0, 1);
    } else {
      set(cr, dark ? (RGBA){1, 1, 1, .12} : (RGBA){1, 1, 1, .95});
      cairo_fill_preserve(cr);
      set(cr, T->line_strong);
      cairo_set_line_width(cr, 1);
      cairo_stroke(cr);
      text(cr, b[i].x + b[i].w / 2, b[i].y + 14, b[i].label, 12.5, false, b[i].enabled ? T->fg : T->fg3, 0, 1);
    }
  }
}

static int hit_btn(const RecBtn *b, int n, int x, int y) {
  for (int i = 0; i < n; i++)
    if (b[i].enabled && x >= b[i].x && x <= b[i].x + b[i].w && y >= b[i].y && y <= b[i].y + 28) return b[i].action;
  return -1;
}

static void icon_at(cairo_t *cr, const char *name, double x, double y, int s) {
  cairo_surface_t *ic = svg_render(name, s, s, NULL);
  if (!ic) return;
  cairo_set_source_surface(cr, ic, x, y);
  cairo_paint(cr);
  cairo_surface_destroy(ic);
}

/* Optionen der ersten Seite: Titel, Beschreibung, Symbol */
static const char *const ROPT[4][3] = {
  {"Install ClokeOS", "Download the current version of ClokeOS from the internet and install it on a disk.",
   "logo-gradient.svg"},
  {"Get Help Online", "Browse the ClokeOS help in a web browser, even when ClokeOS itself does not start.",
   "icons/search.svg"},
  {"Disk Utility", "View disks and their partitions, and erase disks.", "icons/hd.svg"},
  {"Terminal", "Use the command line as the system administrator (root).", "icons/terminal.svg"},
};

/* Platten, auf die installiert werden kann: alle außer dem Installationsmedium */
static int rec_targets(int *idx) {
  int n = 0;
  for (int i = 0; i < nrdisks; i++)
    if (!rdisks[i].installer) idx[n++] = i;
  return n;
}

enum { A_CONT, A_BACK, A_INSTALL, A_GO, A_CANCEL, A_REBOOT, A_HOME };

static int rec_buttons(Client *c, RecBtn *b) {
  int n = 0;
  int t[16];
  switch (rp) {
  case RP_CHOOSE: b[n++] = (RecBtn){L("Continue"), A_CONT, true, rsel >= 0}; break;
  case RP_INTRO:
    b[n++] = (RecBtn){L("Back"), A_HOME, false, true};
    b[n++] = (RecBtn){L("Continue"), A_CONT, true, true};
    break;
  case RP_DISK:
    b[n++] = (RecBtn){L("Back"), A_BACK, false, true};
    b[n++] = (RecBtn){L("Install"), A_INSTALL, true, rdsel >= 0 && rec_targets(t) > 0};
    break;
  case RP_CONFIRM:
    b[n++] = (RecBtn){L("Cancel"), A_CANCEL, false, true};
    b[n++] = (RecBtn){rdsel >= 0 && rdisks[rdsel].root ? L("Reinstall") : L("Erase and Install"), A_GO, true, true,
                      .danger = true};
    break;
  case RP_DONE: b[n++] = (RecBtn){L("Restart"), A_REBOOT, true, true}; break;
  case RP_ERROR: b[n++] = (RecBtn){L("Back"), A_HOME, false, true}; break;
  }
  return layout_btns(c, b, n);
}

static void rec_paint(Client *c, cairo_t *cr) {
  int w = c->w;
  RecBtn b[4];
  int nb = rec_buttons(c, b);
  char buf[512], size[32];
  switch (rp) {
  case RP_CHOOSE: {
    text(cr, w / 2.0, 34, L("ClokeOS Recovery"), 15, true, T->fg, 0, 1);
    for (int i = 0; i < 4; i++) {
      double y = 62 + i * 76;
      bool sel = i == rsel;
      if (sel) {
        rrect(cr, 18, y, w - 36, 68, 12);
        set(cr, ACCENT);
        cairo_fill(cr);
      }
      icon_at(cr, ROPT[i][2], 32, y + 12, 44);
      RGBA fg = sel ? (RGBA){1, 1, 1, 1} : T->fg, fg2 = sel ? (RGBA){1, 1, 1, .85} : T->fg2;
      text(cr, 92, y + 22, L(ROPT[i][0]), 13.5, true, fg, w - 120, 0);
      const char *desc = i == 0 && rec_offline()
                           ? L("Install ClokeOS from this installer on a disk. No internet connection is needed.")
                           : L(ROPT[i][1]);
      para(cr, 92, y + 34, w - 130, desc, 11.5, fg2, false);
    }
    break;
  }
  case RP_INTRO: {
    icon_at(cr, "logo-gradient.svg", (w - 80) / 2.0, 28, 80);
    text(cr, w / 2.0, 134, L("Install ClokeOS"), 18, true, T->fg, 0, 1);
    int y = 160;
    y += para(cr, 60, y, w - 120,
              rec_offline()
                ? L("The installer copies ClokeOS to a disk; no internet connection is needed. Three partitions are "
                    "created there: the startup volume, ClokeOS and a recovery, which you can use to reinstall "
                    "ClokeOS later. The recovery comes from the internet if there is a connection.")
                : L("Recovery downloads the current version of ClokeOS from the internet and installs it on a disk. "
                    "Three partitions are created there: the startup volume, ClokeOS and a recovery like this one, "
                    "which you can use to reinstall ClokeOS later."),
              12.5, T->fg, true) + 14;
    para(cr, 60, y, w - 120, L("All data on the selected disk will be erased."), 12.5, T->fg, true);
    if (rec_offline()) snprintf(buf, sizeof buf, "%s", L("Source: this installer (no internet needed)"));
    else snprintf(buf, sizeof buf, L("Source: %s"), rec_releases);
    para(cr, 40, c->h - 86, w - 80, buf, 10.5, T->fg3, true);
    break;
  }
  case RP_DISK: {
    text(cr, w / 2.0, 34, L("Select the disk where you want to install ClokeOS"), 15, true, T->fg, 0, 1);
    int t[16], nt = rec_targets(t);
    if (!nt) para(cr, 60, 120, w - 120, L("No disk was found that ClokeOS can be installed on."), 12.5,
                  T->fg2, true);
    for (int k = 0; k < nt && k < 4; k++) {
      RecDisk *d = &rdisks[t[k]];
      double y = 62 + k * 70;
      bool sel = t[k] == rdsel;
      if (sel) {
        rrect(cr, 18, y, w - 36, 62, 12);
        set(cr, ACCENT);
        cairo_fill(cr);
      }
      icon_at(cr, "icons/hd.svg", 32, y + 11, 40);
      RGBA fg = sel ? (RGBA){1, 1, 1, 1} : T->fg, fg2 = sel ? (RGBA){1, 1, 1, .85} : T->fg2;
      fmt_size(size, sizeof size, d->bytes);
      snprintf(buf, sizeof buf, "%s · %s", d->label, size);
      text(cr, 88, y + 21, buf, 13.5, true, fg, w - 120, 0);
      const char *sub = d->root ? "This recovery runs from this disk: ClokeOS is replaced, the recovery stays."
                        : d->clokeos ? "Already contains ClokeOS. It will be replaced along with all data."
                        : d->nparts ? "Contains partitions that will be erased."
                                    : "Empty";
      text(cr, 88, y + 42, L(sub), 11.5, false, fg2, w - 120, 0);
    }
    break;
  }
  case RP_CONFIRM: {
    RecDisk *d = &rdisks[rdsel];
    icon_at(cr, "icons/hd.svg", (w - 64) / 2.0, 40, 64);
    fmt_size(size, sizeof size, d->bytes);
    if (d->root) snprintf(buf, sizeof buf, L("Reinstall ClokeOS on “%s”?"), d->label);
    else snprintf(buf, sizeof buf, L("Erase “%s” (%s) and install ClokeOS?"), d->label, size);
    para(cr, 50, 130, w - 100, buf, 15, T->fg, true);
    para(cr, 60, 186, w - 120,
         d->root ? L("The system on this disk will be replaced by the current version; files in it will be lost. "
                     "The recovery stays.")
                 : L("All data on this disk will be lost. This cannot be undone."),
         12.5, T->fg2, true);
    break;
  }
  case RP_RUN: {
    icon_at(cr, "logo-gradient.svg", (w - 72) / 2.0, 56, 72);
    text(cr, w / 2.0, 160, L("Installing ClokeOS"), 18, true, T->fg, 0, 1);
    text(cr, w / 2.0, 196, rstep[0] ? rstep : L("Preparing…"), 12.5, false, T->fg2, w - 80, 1);
    double bw = w - 160, bx = 80, by = 222, frac = rtotal ? (double)rdone / rtotal : 0;
    rrect(cr, bx, by, bw, 8, 4);
    set(cr, dark ? (RGBA){1, 1, 1, .14} : (RGBA){0, 0, 0, .1});
    cairo_fill(cr);
    if (frac > 0) {
      rrect(cr, bx, by, fmax(8, bw * fmin(frac, 1)), 8, 4);
      set(cr, ACCENT);
      cairo_fill(cr);
    }
    if (rtotal) {
      char a[32], z[32], eta[64] = "";
      fmt_size(a, sizeof a, rdone);
      fmt_size(z, sizeof z, rtotal);
      double el = now_s() - rt0;
      if (rdone > 0 && el > 3) {
        double left = (rtotal - rdone) / (rdone / el);
        int m = (int)(left / 60 + .5);
        if (left < 60) snprintf(eta, sizeof eta, "%s", L(" · Less than a minute remaining"));
        else if (m == 1) snprintf(eta, sizeof eta, "%s", L(" · About 1 minute remaining"));
        else snprintf(eta, sizeof eta, L(" · About %d minutes remaining"), m);
      }
      snprintf(buf, sizeof buf, rec_offline() ? L("%s of %s copied%s") : L("%s of %s downloaded%s"), a, z, eta);
      text(cr, w / 2.0, 252, buf, 11.5, false, T->fg3, 0, 1);
    }
    break;
  }
  case RP_DONE: {
    icon_at(cr, "logo-gradient.svg", (w - 80) / 2.0, 56, 80);
    snprintf(buf, sizeof buf, L("ClokeOS %s is installed."), rversion);
    text(cr, w / 2.0, 168, buf, 18, true, T->fg, 0, 1);
    para(cr, 60, 196, w - 120,
         rdsel >= 0 && rdisks[rdsel].root
           ? L("Restart your computer and it starts ClokeOS again. The recovery stays on the disk; you start it "
               "with “Restart in Recovery…” in the ClokeOS menu.")
           : L("Restart your computer. If the installer is still connected, remove it or choose the disk in the "
               "firmware’s boot menu. Later you start the recovery with “Restart in Recovery…” in the ClokeOS "
               "menu."),
         12.5, T->fg2, true);
    break;
  }
  case RP_ERROR: {
    icon_at(cr, "icons/info.svg", (w - 64) / 2.0, 52, 64);
    text(cr, w / 2.0, 150, L("The installation failed."), 17, true, T->fg, 0, 1);
    para(cr, 60, 178, w - 120, rerror, 12.5, T->fg2, true);
    break;
  }
  }
  paint_btns(cr, b, nb);
}

/* Eigene Fenster lassen sich schließen; danach ist der Client freigegeben */
static bool client_alive(Client *c) {
  for (Client *x = clients; x; x = x->next)
    if (x == c) return true;
  return false;
}

static void rec_redraw(void) {
  if (rwin && client_alive(rwin)) draw_own(rwin);
  else rwin = NULL;
}

static void rec_finish(void) {
  if (rec_fd >= 0) close(rec_fd);
  rec_fd = -1;
  rec_pid = 0;
}

static void rec_line(char *line) {
  if (!strncmp(line, "STEP ", 5)) snprintf(rstep, sizeof rstep, "%s", line + 5);
  else if (!strncmp(line, "PROGRESS ", 9)) sscanf(line + 9, "%llu %llu", &rdone, &rtotal);
  else if (!strncmp(line, "ERROR ", 6)) {
    snprintf(rerror, sizeof rerror, "%s", line + 6);
    rp = RP_ERROR;
  } else if (!strncmp(line, "DONE ", 5)) {
    snprintf(rversion, sizeof rversion, "%s", line + 5);
    rp = RP_DONE;
  }
}

static void rec_read(void) {
  if (rec_fd < 0) return;
  for (;;) {
    ssize_t k = read(rec_fd, rec_buf + rec_len, sizeof rec_buf - 1 - rec_len);
    if (k == 0) { /* Installer beendet */
      rec_finish();
      if (rp == RP_RUN) {
        snprintf(rerror, sizeof rerror, "%s", L("The installer quit unexpectedly."));
        rp = RP_ERROR;
      }
      break;
    }
    if (k < 0) break;
    rec_len += (size_t)k;
    char *s = rec_buf, *nl;
    while ((nl = memchr(s, '\n', (size_t)(rec_buf + rec_len - s)))) {
      *nl = 0;
      rec_line(s);
      s = nl + 1;
    }
    rec_len -= (size_t)(s - rec_buf);
    memmove(rec_buf, s, rec_len);
    if (rec_len >= sizeof rec_buf - 1) rec_len = 0;
  }
  rec_redraw();
}

static void rec_start_install(void) {
  int fd[2];
  if (pipe(fd)) return;
  pid_t pid = fork();
  if (pid == 0) {
    setsid();
    signal(SIGCHLD, SIG_DFL);
    close(fd[0]);
    dup2(fd[1], 1);
    if (fd[1] != 1) close(fd[1]);
    execl(INSTALLER, "clokeos-installer", "install", rdisks[rdsel].name, rec_releases, (char *)NULL);
    printf("ERROR %s (%s).\n", L("The installer is missing"), INSTALLER);
    _exit(127);
  }
  close(fd[1]);
  if (pid < 0) {
    close(fd[0]);
    return;
  }
  fcntl(fd[0], F_SETFL, O_NONBLOCK);
  fcntl(fd[0], F_SETFD, FD_CLOEXEC);
  rec_fd = fd[0];
  rec_pid = pid;
  rec_len = 0;
  rdone = rtotal = 0;
  rstep[0] = 0;
  rt0 = now_s();
  rp = RP_RUN;
}

static void show_disk_utility(long unused);

static void rec_open_option(int i) {
  switch (i) {
  case 0:
    rp = RP_INTRO;
    rec_redraw();
    break;
  case 1: { /* Seite des Release-Repos: die Adresse ohne /releases/latest/download */
    char url[600], cmd[700];
    snprintf(url, sizeof url, "%s", rec_offline() ? REC_ONLINE : rec_releases);
    char *r = strstr(url, "/releases");
    if (r) *r = 0;
    snprintf(cmd, sizeof cmd, "netsurf-gtk3 '%s#readme'", url);
    spawn(cmd);
    break;
  }
  case 2: show_disk_utility(0); break;
  case 3:
    if (app_by_ident("com.clokeos.terminal") >= 0) launch_ident("com.clokeos.terminal");
    else spawn("xfce4-terminal");
    break;
  }
}

static void rec_click(Client *c, int x, int y) {
  RecBtn b[4];
  int nb = rec_buttons(c, b), a = hit_btn(b, nb, x, y);
  if (a < 0) {
    if (rp == RP_CHOOSE) {
      for (int i = 0; i < 4; i++)
        if (y >= 62 + i * 76 && y < 62 + i * 76 + 68) {
          static double last;
          static int last_i = -1;
          if (i == last_i && now_s() - last < .4) rec_open_option(i); /* Doppelklick */
          last = now_s();
          last_i = i;
          rsel = i;
        }
    } else if (rp == RP_DISK) {
      int t[16], nt = rec_targets(t);
      for (int k = 0; k < nt && k < 4; k++)
        if (y >= 62 + k * 70 && y < 62 + k * 70 + 62) rdsel = t[k];
    }
    rec_redraw();
    return;
  }
  switch (a) {
  case A_CONT:
    if (rp == RP_CHOOSE) rec_open_option(rsel);
    else if (rp == RP_INTRO) {
      rec_load_disks();
      rdsel = -1;
      int t[16];
      if (rec_targets(t) == 1) rdsel = t[0];
      rp = RP_DISK;
    }
    break;
  case A_HOME: rp = RP_CHOOSE; break;
  case A_BACK: rp = RP_INTRO; break;
  case A_INSTALL: rp = RP_CONFIRM; break;
  case A_CANCEL: rp = RP_DISK; break;
  case A_GO: rec_start_install(); break;
  case A_REBOOT:
    XSync(dpy, False);
    spawn_as("/bin/reboot", true);
    break;
  }
  rec_redraw();
}

static void rec_show(long page) {
  if (rp == RP_RUN) page = RP_RUN; /* eine laufende Installation nicht verlassen */
  rp = (int)page;
  rwin = own_window(RW, RH, "Recovery", rec_paint, rec_click, 0);
}

/* ---- Festplattendienstprogramm ------------------------------------------------------------ */

#define DW 560
#define DH 400

static int du_buttons(Client *c, RecBtn *b) {
  int n = 0;
  RecDisk *d = dsel >= 0 && dsel < nrdisks ? &rdisks[dsel] : NULL;
  if (dconfirm) {
    b[n++] = (RecBtn){L("Cancel"), A_CANCEL, false, true};
    b[n++] = (RecBtn){L("Erase"), A_GO, true, true, .danger = true};
  } else {
    b[n++] = (RecBtn){L("Refresh"), A_HOME, false, true};
    b[n++] = (RecBtn){L("Erase…"), A_CONT, true, d && !d->root && !d->installer};
  }
  return layout_btns(c, b, n);
}

static void du_paint(Client *c, cairo_t *cr) {
  int w = c->w;
  char buf[160], size[32];
  double y = 20;
  for (int i = 0; i < nrdisks && y < c->h - 110; i++) {
    RecDisk *d = &rdisks[i];
    bool sel = i == dsel;
    int rows = 1 + d->nparts;
    if (sel) {
      rrect(cr, 12, y - 4, w - 24, rows * 26 + 8, 10);
      set(cr, dark ? (RGBA){1, 1, 1, .1} : (RGBA){0, 0, 0, .06});
      cairo_fill(cr);
    }
    icon_at(cr, "icons/hd.svg", 22, y, 22);
    fmt_size(size, sizeof size, d->bytes);
    snprintf(buf, sizeof buf, "%s · %s%s", d->label, size,
             d->root ? L(" · Startup disk") : d->installer ? L(" · Installer") : "");
    text(cr, 52, y + 11, buf, 12.5, true, T->fg, w - 80, 0);
    y += 26;
    for (int k = 0; k < d->nparts; k++, y += 26) {
      fmt_size(size, sizeof size, d->part[k].bytes);
      snprintf(buf, sizeof buf, "%s · %s", strcmp(d->part[k].name, "-") ? L(d->part[k].name) : "Partition", size);
      text(cr, 72, y + 11, buf, 12, false, T->fg2, w - 100, 0);
    }
    y += 12;
  }
  if (!nrdisks) text(cr, w / 2.0, 80, L("No disks found."), 12.5, false, T->fg2, 0, 1);
  if (dconfirm && dsel >= 0) {
    fmt_size(size, sizeof size, rdisks[dsel].bytes);
    snprintf(buf, sizeof buf, L("Erase “%s” (%s)? All data on it will be lost."), rdisks[dsel].label, size);
    para(cr, 20, c->h - 96, w - 40, buf, 12.5, T->fg, false);
  } else if (dmsg[0]) {
    para(cr, 20, c->h - 96, w - 40, dmsg, 12, T->fg2, false);
  }
  RecBtn b[3];
  int nb = du_buttons(c, b);
  paint_btns(cr, b, nb);
}

static void du_click(Client *c, int x, int y) {
  RecBtn b[3];
  int nb = du_buttons(c, b), a = hit_btn(b, nb, x, y);
  if (a < 0) {
    double yy = 20;
    for (int i = 0; i < nrdisks; i++) {
      int h = (1 + rdisks[i].nparts) * 26 + 12;
      if (y >= yy - 4 && y < yy + h - 8) {
        dsel = i;
        dconfirm = false;
        dmsg[0] = 0;
      }
      yy += h;
    }
  } else if (a == A_HOME) {
    rec_load_disks();
    dmsg[0] = 0;
  } else if (a == A_CONT) {
    dconfirm = true;
  } else if (a == A_CANCEL) {
    dconfirm = false;
  } else if (a == A_GO && dsel >= 0) {
    char cmd[128], line[256] = "";
    snprintf(cmd, sizeof cmd, INSTALLER " erase %s 2>&1", rdisks[dsel].name);
    child_wait_begin();
    FILE *p = popen(cmd, "r");
    bool ok = false;
    if (p) {
      while (fgets(line, sizeof line, p)) {
        line[strcspn(line, "\n")] = 0;
        if (!strncmp(line, "DONE", 4)) ok = true;
        else if (!strncmp(line, "ERROR ", 6)) snprintf(dmsg, sizeof dmsg, "%s", line + 6);
      }
      pclose(p);
    }
    child_wait_end();
    if (ok) snprintf(dmsg, sizeof dmsg, L("“%s” was erased."), rdisks[dsel].label);
    dconfirm = false;
    rec_load_disks();
  }
  draw_own(c);
}

static void show_disk_utility(long unused) {
  (void)unused;
  rec_load_disks();
  dsel = -1;
  dconfirm = false;
  dmsg[0] = 0;
  dwin = own_window(DW, DH, "Disk Utility", du_paint, du_click, 0);
}

/* ---- Menüs ---------------------------------------------------------------------------------- */

static void rec_menu_install(long unused) { (void)unused; rec_show(RP_INTRO); }
static void rec_menu_help(long unused) { (void)unused; rec_open_option(1); }
static void rec_menu_terminal(long unused) { (void)unused; rec_open_option(3); }
