/* Festplattendienstprogramm wie in macOS: links die Datenträger mit ihren Partitionen, rechts die Aufteilung als
 * Balken, darunter die Liste und die Werkzeuge Hinzufügen, Löschen, Größe ändern und Entfernen. Wird in
 * cloakserver.c nach recovery.c eingebunden und nutzt dessen Zeichenhelfer. Läuft in der Wiederherstellung
 * (Menü "Dienstprogramme") und im System (Programme/Dienstprogramme/Disk Utility).
 *
 * Alle Eingriffe macht clokeos-installer (recovery/installer.c: layout, add, delete, format, resize, erase);
 * die GPT, FAT32 und ext4 schreibt er selbst, ext4 über die e2fsprogs in /usr/sbin. Eine Platte, von der das
 * System gerade läuft, lässt sich nicht ändern (wie bei macOS die Startvolume). */

#define DW 820
#define DH 580
#define DSB 220 /* Breite der Seitenleiste */
#define MIB_ (1ull << 20)

typedef struct {
  int slot;
  unsigned long long first, count; /* in Blöcken */
  char fs[12], flags[8], name[48];
} DuPart;
typedef struct {
  unsigned long long first, count;
} DuFree;
typedef struct {
  char name[16], label[64];
  unsigned long long bytes;
  unsigned bsize;
  bool root, installer, clokeos, gpt;
  int np, nf;
  DuPart part[32];
  DuFree fr[32];
} DuDisk;
typedef struct {
  bool free_;
  int idx;
  unsigned long long first;
} DuRow;

static DuDisk dud[16];
static int ndud, du_d = -1, du_r = -1; /* gewählte Platte und Zeile (-1: die Platte selbst) */
static char dmsg[240];
enum { DM_NONE, DM_ADD, DM_ERASE, DM_RESIZE, DM_DELETE, DM_ERASE_DISK };
static int du_mode, du_focus, du_fmt;
static char du_name[48], du_size[24];
static double du_t0;
enum { DA_ADD = 1, DA_ERASE, DA_RESIZE, DA_DELETE, DA_REFRESH, DA_OK, DA_CANCEL, DA_EJECT };

/* Disk-Images, die clokeos-dmg unter /Volumes ausgepackt hat (dmg/dmg.c) */
#define DMGTOOL "/usr/libexec/clokeos-dmg"
static char du_img[8][300];
static int ndimg, du_ii = -1;

static void du_load(void) {
  char keep[16] = "";
  if (du_d >= 0 && du_d < ndud) snprintf(keep, sizeof keep, "%s", dud[du_d].name);
  ndud = 0;
  child_wait_begin();
  FILE *p = popen(INSTALLER " disks 2>/dev/null", "r");
  char names[16][16];
  int nn = 0;
  if (p) {
    char line[256];
    while (fgets(line, sizeof line, p))
      if (!strncmp(line, "DISK ", 5) && nn < 16 && sscanf(line + 5, "%15s", names[nn]) == 1) nn++;
    pclose(p);
  }
  for (int i = 0; i < nn; i++) {
    char cmd[96], line[256];
    snprintf(cmd, sizeof cmd, INSTALLER " layout %s 2>/dev/null", names[i]);
    p = popen(cmd, "r");
    if (!p) continue;
    DuDisk *d = &dud[ndud];
    memset(d, 0, sizeof *d);
    bool ok = false;
    while (fgets(line, sizeof line, p)) {
      line[strcspn(line, "\n")] = 0;
      char flags[8];
      int off = 0, slot;
      unsigned long long a, b;
      char fs[12], pf[8];
      if (sscanf(line, "DISKX %15s %llu %u %7s %n", d->name, &d->bytes, &d->bsize, flags, &off) == 4) {
        snprintf(d->label, sizeof d->label, "%s", line + off);
        d->root = strchr(flags, 'r');
        d->installer = strchr(flags, 'i');
        d->clokeos = strchr(flags, 'c');
        d->gpt = strchr(flags, 'g');
        ok = true;
      } else if (ok && sscanf(line, "PARTX %d %llu %llu %11s %7s %n", &slot, &a, &b, fs, pf, &off) == 5 && d->np < 32) {
        DuPart *q = &d->part[d->np++];
        q->slot = slot, q->first = a, q->count = b;
        snprintf(q->fs, sizeof q->fs, "%s", fs);
        snprintf(q->flags, sizeof q->flags, "%s", pf);
        snprintf(q->name, sizeof q->name, "%s", line + off);
      } else if (ok && sscanf(line, "FREEX %llu %llu", &a, &b) == 2 && d->nf < 32) {
        d->fr[d->nf++] = (DuFree){a, b};
      }
    }
    pclose(p);
    if (ok) ndud++;
  }
  ndimg = 0;
  p = access(DMGTOOL, X_OK) ? NULL : popen(DMGTOOL " list 2>/dev/null", "r");
  if (p) {
    char line[320];
    while (fgets(line, sizeof line, p))
      if (!strncmp(line, "VOLUME ", 7) && ndimg < 8) {
        line[strcspn(line, "\n")] = 0;
        snprintf(du_img[ndimg++], sizeof du_img[0], "%s", line + 7);
      }
    pclose(p);
  }
  child_wait_end();
  du_ii = -1;
  du_d = -1;
  for (int i = 0; i < ndud; i++)
    if (!strcmp(dud[i].name, keep)) du_d = i;
  if (du_d < 0 && ndud) du_d = 0;
  du_r = -1;
}

/* Teile einer Platte nach Lage geordnet */
static int du_rows(const DuDisk *d, DuRow *r) {
  int n = 0;
  for (int i = 0; i < d->np; i++) r[n++] = (DuRow){false, i, d->part[i].first};
  for (int i = 0; i < d->nf; i++) r[n++] = (DuRow){true, i, d->fr[i].first};
  for (int i = 1; i < n; i++)
    for (int k = i; k > 0 && r[k].first < r[k - 1].first; k--) {
      DuRow t = r[k];
      r[k] = r[k - 1];
      r[k - 1] = t;
    }
  return n;
}

static RGBA du_color(const DuPart *p) {
  if (strchr(p->flags, 'e')) return (RGBA){.30, .72, .45, 1};
  if (!strcmp(p->fs, "ext4")) return (RGBA){.94, .60, .22, 1};
  if (!strncmp(p->fs, "fat", 3)) return (RGBA){.27, .55, .92, 1};
  if (!strcmp(p->fs, "hfs") || !strcmp(p->fs, "apfs")) return (RGBA){.62, .42, .88, 1};
  return (RGBA){.58, .58, .64, 1};
}

static const char *du_fs_name(const char *fs) {
  if (!strcmp(fs, "fat32")) return "FAT32";
  if (!strcmp(fs, "fat")) return "FAT";
  if (!strcmp(fs, "ext4")) return "ext4";
  if (!strcmp(fs, "hfs")) return "Mac OS Extended";
  if (!strcmp(fs, "apfs")) return "APFS";
  if (!strcmp(fs, "exfat")) return "ExFAT";
  if (!strcmp(fs, "ntfs")) return "NTFS";
  return L("Unformatted");
}

static const char *du_part_name(const DuPart *p) { return strcmp(p->name, "-") ? L(p->name) : L("Untitled"); }

static unsigned long long du_bytes(const DuDisk *d, unsigned long long blocks) { return blocks * d->bsize; }

/* "2.5 GB", "800", "1G": ohne Einheit MB */
static unsigned long long du_parse(const char *s) {
  char *e;
  double v = strtod(s, &e);
  if (v <= 0) return 0;
  while (*e == ' ') e++;
  double u = MIB_;
  if (*e == 'g' || *e == 'G') u = MIB_ * 1024.;
  else if (*e == 't' || *e == 'T') u = MIB_ * 1048576.;
  else if (*e == 'k' || *e == 'K') u = 1024.;
  return (unsigned long long)(v * u);
}

static void du_size_text(unsigned long long bytes) {
  if (bytes >= 1024 * MIB_) snprintf(du_size, sizeof du_size, "%.2f GB", bytes / (1024. * MIB_));
  else snprintf(du_size, sizeof du_size, "%llu MB", bytes / MIB_);
}

/* ---- Auswahl und Werkzeuge ---- */

static bool du_editable(const DuDisk *d) { return d && !d->root && !d->installer; }

static const DuPart *du_sel_part(void) {
  if (du_d < 0 || du_r < 0) return NULL;
  DuRow r[64];
  int n = du_rows(&dud[du_d], r);
  return du_r < n && !r[du_r].free_ ? &dud[du_d].part[r[du_r].idx] : NULL;
}

static const DuFree *du_sel_free(void) {
  DuDisk *d = du_d >= 0 ? &dud[du_d] : NULL;
  if (!d) return NULL;
  DuRow r[64];
  int n = du_rows(d, r);
  if (du_r >= 0 && du_r < n) return r[du_r].free_ ? &d->fr[r[du_r].idx] : NULL;
  const DuFree *best = NULL; /* Platte gewählt: der größte freie Bereich */
  for (int i = 0; i < d->nf; i++)
    if (!best || d->fr[i].count > best->count) best = &d->fr[i];
  return best;
}

static bool du_enabled(int a) {
  DuDisk *d = du_d >= 0 ? &dud[du_d] : NULL;
  if (a == DA_REFRESH) return true;
  if (a == DA_EJECT) return du_ii >= 0;
  if (du_ii >= 0 || !du_editable(d)) return false;
  const DuPart *p = du_sel_part();
  bool busy_root = p && strchr(p->flags, 'r');
  switch (a) {
  case DA_ADD: return d->gpt && du_sel_free();
  case DA_ERASE: return p ? !busy_root : du_r < 0;
  case DA_RESIZE: return p && !busy_root && (!strcmp(p->fs, "ext4") || !strcmp(p->fs, "-"));
  case DA_DELETE: return p && !busy_root;
  }
  return false;
}

static void du_paint(Client *c, cairo_t *cr);

static void du_busy(const char *msg) {
  snprintf(dmsg, sizeof dmsg, "%s", msg);
  du_mode = DM_NONE;
  Client *c = NULL;
  for (Client *k = clients; k; k = k->next)
    if (k->draw == du_paint) c = k;
  if (c) draw_own(c);
  XFlush(dpy);
}

/* Befehl des Installers ausführen; true bei DONE, sonst steht der Fehler in dmsg */
static bool du_run(const char *args) {
  char cmd[640], line[400];
  snprintf(cmd, sizeof cmd, INSTALLER " %s 2>&1", args);
  child_wait_begin();
  FILE *p = popen(cmd, "r");
  bool ok = false;
  dmsg[0] = 0;
  if (p) {
    while (fgets(line, sizeof line, p)) {
      line[strcspn(line, "\n")] = 0;
      if (!strncmp(line, "DONE", 4)) ok = true;
      else if (!strncmp(line, "ERROR ", 6)) snprintf(dmsg, sizeof dmsg, "%s", line + 6);
    }
    pclose(p);
  }
  child_wait_end();
  if (!ok && !dmsg[0]) snprintf(dmsg, sizeof dmsg, "%s", L("The operation failed."));
  return ok;
}

static void du_start(int mode) {
  DuDisk *d = du_d >= 0 ? &dud[du_d] : NULL;
  if (!d) return;
  du_mode = mode;
  du_focus = mode == DM_RESIZE ? 1 : 0;
  du_t0 = now_s();
  dmsg[0] = 0;
  const DuPart *p = du_sel_part();
  if (mode == DM_ADD) {
    const DuFree *f = du_sel_free();
    snprintf(du_name, sizeof du_name, "%s", L("Untitled"));
    du_size_text(du_bytes(d, f->count));
    du_fmt = 0;
  } else if (mode == DM_ERASE) {
    snprintf(du_name, sizeof du_name, "%s", strcmp(p->name, "-") ? L(p->name) : L("Untitled"));
    du_fmt = !strcmp(p->fs, "ext4") ? 1 : 0;
  } else if (mode == DM_RESIZE) {
    du_size_text(du_bytes(d, p->count));
  }
}

static void du_ok(void) {
  DuDisk *d = du_d >= 0 ? &dud[du_d] : NULL;
  if (!d) return;
  const DuPart *p = du_sel_part();
  char args[600], q[160];
  int mode = du_mode;
  q[0] = 0;
  for (size_t i = 0, k = 0; du_name[i] && k + 1 < sizeof q; i++) /* nur, was in einfachen Anführungszeichen sicher ist */
    if (du_name[i] != '\'' && du_name[i] != '\\') q[k++] = du_name[i], q[k] = 0;
  const char *fs = du_fmt == 0 ? "fat32" : du_fmt == 1 ? "ext4" : "none";
  bool ok = false;
  if (mode == DM_ADD) {
    const DuFree *f = du_sel_free();
    unsigned long long bytes = du_parse(du_size), max = du_bytes(d, f->count);
    if (!bytes) {
      du_mode = DM_ADD;
      snprintf(dmsg, sizeof dmsg, "%s", L("Enter a size, for example 2 GB or 500 MB."));
      return;
    }
    if (bytes > max) bytes = max;
    snprintf(args, sizeof args, "add %s %llu %llu %s '%s'", d->name, f->first, bytes, fs, q);
    du_busy(L("Adding the partition…"));
    ok = du_run(args);
  } else if (mode == DM_ERASE && p) {
    snprintf(args, sizeof args, "format %s %d %s '%s'", d->name, p->slot, fs, q);
    du_busy(L("Erasing the partition…"));
    ok = du_run(args);
  } else if (mode == DM_ERASE_DISK) {
    snprintf(args, sizeof args, "erase %s", d->name);
    du_busy(L("Erasing the disk…"));
    ok = du_run(args);
  } else if (mode == DM_DELETE && p) {
    snprintf(args, sizeof args, "delete %s %d", d->name, p->slot);
    du_busy(L("Removing the partition…"));
    ok = du_run(args);
  } else if (mode == DM_RESIZE && p) {
    unsigned long long bytes = du_parse(du_size);
    if (!bytes) {
      du_mode = DM_RESIZE;
      snprintf(dmsg, sizeof dmsg, "%s", L("Enter a size, for example 2 GB or 500 MB."));
      return;
    }
    snprintf(args, sizeof args, "resize %s %d %llu", d->name, p->slot, bytes);
    du_busy(L("Resizing the partition…"));
    ok = du_run(args);
  }
  char err[240];
  snprintf(err, sizeof err, "%s", dmsg);
  du_mode = DM_NONE;
  du_load();
  if (ok) snprintf(dmsg, sizeof dmsg, "%s", L("Done."));
  else snprintf(dmsg, sizeof dmsg, "%s", err);
}

/* ---- Zeichnen ---- */

typedef struct {
  double x, y, w, h;
} Rc;

static bool in_rc(Rc r, int x, int y) { return x >= r.x && x <= r.x + r.w && y >= r.y && y <= r.y + r.h; }

/* Lage der Felder im unteren Bereich */
static void du_panel(Rc *name, Rc *size, Rc seg[3]) {
  double x0 = DSB + 24, y0 = 392;
  *name = (Rc){x0 + 110, y0 + 34, 280, 26};
  *size = (Rc){x0 + 110, y0 + 70, 150, 26};
  for (int i = 0; i < 3; i++) seg[i] = (Rc){x0 + 110 + i * 112, du_mode == DM_ADD ? y0 + 106 : y0 + 70, 108, 26};
  if (du_mode == DM_ERASE) *size = (Rc){0, 0, 0, 0};
  if (du_mode == DM_RESIZE) *name = (Rc){0, 0, 0, 0};
}

static void du_field(cairo_t *cr, Rc r, const char *s, bool focus) {
  rrect(cr, r.x, r.y, r.w, r.h, 6);
  set(cr, dark ? (RGBA){1, 1, 1, .08} : (RGBA){1, 1, 1, .95});
  cairo_fill_preserve(cr);
  set(cr, focus ? ACCENT : T->line_strong);
  cairo_set_line_width(cr, focus ? 2 : 1);
  cairo_stroke(cr);
  char buf[64];
  snprintf(buf, sizeof buf, "%s%s", s, focus && fmod(now_s(), 1.0) < .6 ? "|" : "");
  text(cr, r.x + 9, r.y + r.h / 2, buf, 12.5, false, T->fg, (int)r.w - 16, 0);
}

static int du_toolbar(RecBtn *b) {
  const struct { const char *l; int a; } t[] = {{L("Add Partition"), DA_ADD}, {L("Erase"), DA_ERASE}, {L("Resize"), DA_RESIZE},
                                                {L("Remove"), DA_DELETE}, {L("Eject"), DA_EJECT}, {L("Refresh"), DA_REFRESH}};
  double x = DSB + 24;
  for (int i = 0; i < 6; i++) {
    b[i] = (RecBtn){t[i].l, t[i].a, false, du_mode == DM_NONE && du_enabled(t[i].a)};
    b[i].w = text_width(b[i].label, 12.5, false) + 28;
    b[i].x = x;
    b[i].y = 14;
    x += b[i].w + 8;
  }
  return 6;
}

static int du_dialog_buttons(Client *c, RecBtn *b) {
  int n = 0;
  if (du_mode == DM_NONE) return 0;
  bool danger = du_mode == DM_DELETE || du_mode == DM_ERASE_DISK || du_mode == DM_ERASE;
  b[n++] = (RecBtn){L("Cancel"), DA_CANCEL, false, true};
  const char *ok = du_mode == DM_ADD ? L("Add") : du_mode == DM_RESIZE ? L("Resize") : du_mode == DM_DELETE ? L("Remove") : L("Erase");
  b[n++] = (RecBtn){ok, DA_OK, true, true, .danger = danger};
  return layout_btns(c, b, n);
}

static void du_paint(Client *c, cairo_t *cr) {
  int w = c->w, h = c->h;
  char buf[200], size[32], size2[32];
  /* Seitenleiste */
  set(cr, dark ? (RGBA){1, 1, 1, .04} : (RGBA){.1, .05, .3, .04});
  cairo_rectangle(cr, 0, 0, DSB, h);
  cairo_fill(cr);
  set(cr, T->line);
  cairo_rectangle(cr, DSB, 0, 1, h);
  cairo_fill(cr);
  double y = 14;
  text(cr, 18, y + 8, L("Disks"), 11, true, T->fg3, 0, 0);
  y += 26;
  for (int i = 0; i < ndud && y < h - 20; i++) {
    DuDisk *d = &dud[i];
    bool sel = i == du_d && du_r < 0;
    if (sel) {
      rrect(cr, 8, y - 2, DSB - 16, 38, 8);
      set(cr, dark ? (RGBA){1, 1, 1, .12} : (RGBA){0, 0, 0, .07});
      cairo_fill(cr);
    }
    icon_at(cr, "icons/hd.svg", 16, y + 4, 26);
    fmt_size(size, sizeof size, d->bytes);
    text(cr, 50, y + 10, d->label, 12.5, true, T->fg, DSB - 64, 0);
    snprintf(buf, sizeof buf, "%s · %s%s", d->name, size, d->root ? L(" · Startup disk") : d->installer ? L(" · Installer") : "");
    text(cr, 50, y + 26, buf, 10.5, false, T->fg2, DSB - 64, 0);
    y += 40;
    DuRow rows[64];
    int nr = du_rows(d, rows);
    for (int k = 0; k < nr; k++) {
      if (rows[k].free_) continue;
      DuPart *p = &d->part[rows[k].idx];
      bool ps = i == du_d && du_r == k;
      if (ps) {
        rrect(cr, 18, y - 2, DSB - 26, 22, 7);
        set(cr, dark ? (RGBA){1, 1, 1, .12} : (RGBA){0, 0, 0, .07});
        cairo_fill(cr);
      }
      cairo_arc(cr, 32, y + 9, 4.5, 0, 6.2832);
      set(cr, du_color(p));
      cairo_fill(cr);
      text(cr, 44, y + 9, du_part_name(p), 11.5, false, T->fg, DSB - 64, 0);
      y += 24;
    }
    y += 8;
  }
  if (!ndud) text(cr, DSB / 2.0, 90, L("No disks found."), 12, false, T->fg2, 0, 1);
  if (ndimg) {
    text(cr, 18, y + 8, L("Disk Images"), 11, true, T->fg3, 0, 0);
    y += 26;
    for (int i = 0; i < ndimg; i++, y += 28) {
      if (i == du_ii) {
        rrect(cr, 8, y - 2, DSB - 16, 26, 8);
        set(cr, dark ? (RGBA){1, 1, 1, .12} : (RGBA){0, 0, 0, .07});
        cairo_fill(cr);
      }
      icon_at(cr, "icons/hd.svg", 16, y, 20);
      const char *base = strrchr(du_img[i], '/');
      text(cr, 44, y + 10, base ? base + 1 : du_img[i], 12, false, T->fg, DSB - 58, 0);
    }
  }

  /* Werkzeugleiste */
  RecBtn tb[6];
  int ntb = du_toolbar(tb);
  paint_btns(cr, tb, ntb);

  DuDisk *d = du_d >= 0 && du_ii < 0 ? &dud[du_d] : NULL;
  double x0 = DSB + 24, mw = w - DSB - 48;
  if (du_ii >= 0) {
    const char *base = strrchr(du_img[du_ii], '/');
    icon_at(cr, "icons/hd.svg", x0, 62, 40);
    text(cr, x0 + 52, 74, base ? base + 1 : du_img[du_ii], 17, true, T->fg, (int)mw - 60, 0);
    text(cr, x0 + 52, 94, L("Disk image (opened from a .dmg file)"), 11.5, false, T->fg2, (int)mw - 60, 0);
    para(cr, x0, 130, (int)mw, L("The files of this disk image were copied to the folder shown below and are read-only. Eject removes them again; the .dmg file itself stays."), 12, T->fg2, false);
    text(cr, x0, 200, du_img[du_ii], 12, false, T->fg3, (int)mw, 0);
    if (dmsg[0]) para(cr, x0, 240, (int)mw, dmsg, 12, T->fg, false);
  }
  if (d) {
    fmt_size(size, sizeof size, d->bytes);
    icon_at(cr, "icons/hd.svg", x0, 62, 40);
    text(cr, x0 + 52, 74, d->label, 17, true, T->fg, (int)mw - 60, 0);
    snprintf(buf, sizeof buf, "%s · %s · %s%s", d->name, size, d->gpt ? "GUID" : L("No partition table"),
             d->root ? L(" · Startup disk") : "");
    text(cr, x0 + 52, 94, buf, 11.5, false, T->fg2, (int)mw - 60, 0);

    /* Balken */
    double by = 118, bh = 46, total = d->bytes ? (double)d->bytes : 1;
    DuRow rows[64];
    int nr = du_rows(d, rows);
    rrect(cr, x0, by, mw, bh, 9);
    set(cr, dark ? (RGBA){1, 1, 1, .06} : (RGBA){0, 0, 0, .05});
    cairo_fill_preserve(cr);
    cairo_save(cr);
    cairo_clip(cr);
    double cx = x0;
    for (int k = 0; k < nr; k++) {
      unsigned long long cnt = rows[k].free_ ? d->fr[rows[k].idx].count : d->part[rows[k].idx].count;
      double segw = du_bytes(d, cnt) / total * mw;
      if (segw < 3) segw = 3;
      double sx = x0 + du_bytes(d, rows[k].first) / total * mw;
      cx = sx;
      if (!rows[k].free_) {
        set(cr, du_color(&d->part[rows[k].idx]));
        cairo_rectangle(cr, sx, by, segw, bh);
        cairo_fill(cr);
      }
      if (du_r == k) {
        set(cr, ACCENT);
        cairo_set_line_width(cr, 3);
        cairo_rectangle(cr, sx + 1.5, by + 1.5, segw - 3, bh - 3);
        cairo_stroke(cr);
      }
      set(cr, T->win);
      cairo_rectangle(cr, sx + segw - 1, by, 1.5, bh); /* Trennlinie */
      cairo_fill(cr);
    }
    (void)cx;
    cairo_restore(cr);
    cairo_new_path(cr);

    /* Liste */
    double ty = 182;
    text(cr, x0 + 22, ty + 8, L("Name"), 10.5, true, T->fg3, 0, 0);
    text(cr, x0 + 270, ty + 8, L("Format"), 10.5, true, T->fg3, 0, 0);
    text(cr, x0 + mw - 10, ty + 8, L("Capacity"), 10.5, true, T->fg3, 0, 2);
    ty += 22;
    for (int k = 0; k < nr && ty < 372; k++, ty += 24) {
      bool sel = du_r == k;
      if (sel) {
        rrect(cr, x0 - 4, ty - 1, mw + 8, 23, 7);
        set(cr, dark ? (RGBA){1, 1, 1, .12} : (RGBA){0, 0, 0, .07});
        cairo_fill(cr);
      }
      if (rows[k].free_) {
        DuFree *f = &d->fr[rows[k].idx];
        text(cr, x0 + 22, ty + 10, L("Free space"), 12, false, T->fg2, 240, 0);
        fmt_size(size, sizeof size, du_bytes(d, f->count));
        text(cr, x0 + mw - 10, ty + 10, size, 12, false, T->fg2, 0, 2);
      } else {
        DuPart *p = &d->part[rows[k].idx];
        cairo_arc(cr, x0 + 9, ty + 10, 5, 0, 6.2832);
        set(cr, du_color(p));
        cairo_fill(cr);
        snprintf(buf, sizeof buf, "%s%s", du_part_name(p), strchr(p->flags, 'm') ? " ⏏" : "");
        text(cr, x0 + 22, ty + 10, buf, 12, false, T->fg, 240, 0);
        text(cr, x0 + 270, ty + 10, du_fs_name(p->fs), 12, false, T->fg2, 150, 0);
        fmt_size(size, sizeof size, du_bytes(d, p->count));
        text(cr, x0 + mw - 10, ty + 10, size, 12, false, T->fg2, 0, 2);
      }
    }
    if (!d->gpt)
      para(cr, x0, 200, (int)mw, L("This disk has no partition table. Erase it to set it up, then add partitions."), 12.5, T->fg2, false);

    /* unterer Bereich: Einzelheiten oder Eingaben */
    double py = 392;
    set(cr, T->line);
    cairo_rectangle(cr, x0, py - 12, mw, 1);
    cairo_fill(cr);
    const DuPart *p = du_sel_part();
    if (du_mode == DM_NONE) {
      if (p) {
        snprintf(buf, sizeof buf, "%s", du_part_name(p));
        text(cr, x0, py + 8, buf, 14, true, T->fg, (int)mw, 0);
        fmt_size(size, sizeof size, du_bytes(d, p->count));
        fmt_size(size2, sizeof size2, du_bytes(d, p->first));
        snprintf(buf, sizeof buf, "/dev/%ss%d", d->name, p->slot);
        char l1[200], l2[200];
        snprintf(l1, sizeof l1, "%s: %s", L("Device"), buf);
        snprintf(l2, sizeof l2, "%s: %s   ·   %s: %s", L("Format"), du_fs_name(p->fs), L("Capacity"), size);
        text(cr, x0, py + 32, l1, 12, false, T->fg2, (int)mw, 0);
        text(cr, x0, py + 52, l2, 12, false, T->fg2, (int)mw, 0);
        snprintf(l2, sizeof l2, "%s", strchr(p->flags, 'r') ? L("In use: the system is running from this partition.")
                                    : strchr(p->flags, 'm') ? L("Mounted") : L("Not mounted"));
        text(cr, x0, py + 72, l2, 12, false, T->fg2, (int)mw, 0);
      } else if (du_r < 0) {
        text(cr, x0, py + 8, d->label, 14, true, T->fg, (int)mw, 0);
        const char *hint = !du_editable(d) ? L("The system is running from this disk or it is the installer, so its partitions can’t be changed.")
                                           : L("Select a partition or free space, then choose a tool above. Erase with the disk selected sets up a new partition table.");
        para(cr, x0, py + 30, (int)mw, hint, 12, T->fg2, false);
      } else {
        text(cr, x0, py + 8, L("Free space"), 14, true, T->fg, (int)mw, 0);
        para(cr, x0, py + 30, (int)mw, L("Choose Add Partition to use this space."), 12, T->fg2, false);
      }
      if (dmsg[0]) para(cr, x0, py + 98, (int)mw, dmsg, 12, T->fg, false);
    } else {
      Rc nm, sz, seg[3];
      du_panel(&nm, &sz, seg);
      const char *title = du_mode == DM_ADD ? L("Add a partition")
                          : du_mode == DM_ERASE ? L("Erase the partition")
                          : du_mode == DM_RESIZE ? L("Resize the partition")
                          : du_mode == DM_DELETE ? L("Remove the partition") : L("Erase the disk");
      text(cr, x0, py + 8, title, 14, true, T->fg, (int)mw, 0);
      if (du_mode == DM_DELETE && p) {
        snprintf(buf, sizeof buf, L("Remove “%s”? All data on it will be lost."), du_part_name(p));
        para(cr, x0, py + 34, (int)mw, buf, 12.5, T->fg, false);
      } else if (du_mode == DM_ERASE_DISK) {
        fmt_size(size, sizeof size, d->bytes);
        snprintf(buf, sizeof buf, L("Erase “%s” (%s)? All data on it will be lost."), d->label, size);
        para(cr, x0, py + 34, (int)mw, buf, 12.5, T->fg, false);
      } else {
        if (nm.w) {
          text(cr, x0, nm.y + 13, L("Name"), 12, false, T->fg2, 100, 0);
          du_field(cr, nm, du_name, du_focus == 0);
        }
        if (sz.w) {
          text(cr, x0, sz.y + 13, L("Size"), 12, false, T->fg2, 100, 0);
          du_field(cr, sz, du_size, du_focus == 1);
          if (du_mode == DM_ADD) {
            const DuFree *f = du_sel_free();
            fmt_size(size, sizeof size, du_bytes(d, f->count));
            snprintf(buf, sizeof buf, L("up to %s"), size);
            text(cr, sz.x + sz.w + 12, sz.y + 13, buf, 11.5, false, T->fg3, 0, 0);
          }
        }
        if (du_mode != DM_RESIZE) {
          text(cr, x0, seg[0].y + 13, L("Format"), 12, false, T->fg2, 100, 0);
          const char *lab[3] = {"FAT32", "ext4", L("Unformatted")};
          for (int i = 0; i < (du_mode == DM_ADD ? 3 : 2); i++) {
            rrect(cr, seg[i].x, seg[i].y, seg[i].w, seg[i].h, 13);
            if (du_fmt == i) {
              set(cr, ACCENT);
              cairo_fill(cr);
            } else {
              set(cr, dark ? (RGBA){1, 1, 1, .1} : (RGBA){1, 1, 1, .95});
              cairo_fill_preserve(cr);
              set(cr, du_focus == 2 && du_fmt == i ? ACCENT : T->line_strong);
              cairo_set_line_width(cr, 1);
              cairo_stroke(cr);
            }
            text(cr, seg[i].x + seg[i].w / 2, seg[i].y + 13, lab[i], 12, false, du_fmt == i ? (RGBA){1, 1, 1, 1} : T->fg,
                 (int)seg[i].w - 8, 1);
          }
        }
        if (du_mode == DM_ERASE && p) {
          snprintf(buf, sizeof buf, "%s", L("All data on the partition will be lost."));
          text(cr, x0, py + 112, buf, 11.5, false, T->fg3, (int)mw, 0);
        }
      }
      if (dmsg[0]) para(cr, x0, py + 130, (int)mw, dmsg, 12, T->fg, false);
    }
  }
  RecBtn b[3];
  int nb = du_dialog_buttons(c, b);
  paint_btns(cr, b, nb);
}

/* ---- Eingabe ---- */

static void du_do(int a) {
  switch (a) {
  case DA_REFRESH:
    du_load();
    dmsg[0] = 0;
    break;
  case DA_ADD:
    if (du_enabled(DA_ADD)) du_start(DM_ADD);
    break;
  case DA_ERASE:
    if (!du_enabled(DA_ERASE)) break;
    if (du_sel_part()) du_start(DM_ERASE);
    else {
      du_mode = DM_ERASE_DISK;
      du_t0 = now_s();
      dmsg[0] = 0;
    }
    break;
  case DA_RESIZE:
    if (du_enabled(DA_RESIZE)) du_start(DM_RESIZE);
    break;
  case DA_DELETE:
    if (du_enabled(DA_DELETE)) {
      du_mode = DM_DELETE;
      du_t0 = now_s();
      dmsg[0] = 0;
    }
    break;
  case DA_CANCEL:
    du_mode = DM_NONE;
    dmsg[0] = 0;
    break;
  case DA_EJECT:
    if (du_ii >= 0) {
      char args[400], line[400], path[300];
      snprintf(path, sizeof path, "%s", du_img[du_ii]);
      for (char *q = path; *q; q++)
        if (*q == '\'') *q = '_'; /* in einfachen Anführungszeichen unmöglich; solche Namen legt clokeos-dmg nicht an */
      snprintf(args, sizeof args, DMGTOOL " eject '%s' 2>&1", path);
      child_wait_begin();
      FILE *p = popen(args, "r");
      bool ok = false;
      dmsg[0] = 0;
      while (p && fgets(line, sizeof line, p)) {
        line[strcspn(line, "\n")] = 0;
        if (!strncmp(line, "EJECTED", 7)) ok = true;
        else if (!strncmp(line, "ERROR ", 6)) snprintf(dmsg, sizeof dmsg, "%s", line + 6);
      }
      if (p) pclose(p);
      child_wait_end();
      du_load();
      if (ok) snprintf(dmsg, sizeof dmsg, "%s", L("Done."));
    }
    break;
  case DA_OK:
    if ((du_mode == DM_DELETE || du_mode == DM_ERASE_DISK || du_mode == DM_ERASE) && now_s() - du_t0 < .5) break;
    du_ok();
    break;
  }
}

static void du_select(int d, int r) {
  du_ii = -1;
  du_d = d;
  du_r = r;
  du_mode = DM_NONE;
  dmsg[0] = 0;
}

static void du_click(Client *c, int x, int y) {
  RecBtn tb[6], b[3];
  int ntb = du_toolbar(tb), nb = du_dialog_buttons(c, b);
  int a = hit_btn(b, nb, x, y);
  if (a < 0) a = hit_btn(tb, ntb, x, y);
  if (a >= 0) {
    du_do(a);
    draw_own(c);
    return;
  }
  if (du_mode != DM_NONE) {
    Rc nm, sz, seg[3];
    du_panel(&nm, &sz, seg);
    if (nm.w && in_rc(nm, x, y)) du_focus = 0;
    else if (sz.w && in_rc(sz, x, y)) du_focus = 1;
    for (int i = 0; i < (du_mode == DM_ADD ? 3 : 2) && du_mode != DM_RESIZE; i++)
      if (in_rc(seg[i], x, y)) du_fmt = i, du_focus = 2;
    draw_own(c);
    return;
  }
  if (x < DSB) { /* Seitenleiste */
    double yy = 40;
    for (int i = 0; i < ndud; i++) {
      if (y >= yy - 2 && y < yy + 38) du_select(i, -1);
      yy += 40;
      DuRow rows[64];
      int nr = du_rows(&dud[i], rows);
      for (int k = 0; k < nr; k++) {
        if (rows[k].free_) continue;
        if (y >= yy - 2 && y < yy + 22) du_select(i, k);
        yy += 24;
      }
      yy += 8;
    }
    if (ndimg) {
      yy += 26;
      for (int i = 0; i < ndimg; i++, yy += 28)
        if (y >= yy - 2 && y < yy + 24) {
          du_select(du_d, du_r);
          du_ii = i;
        }
    }
  } else if (du_d >= 0 && du_ii < 0) { /* Balken und Liste */
    DuDisk *d = &dud[du_d];
    DuRow rows[64];
    int nr = du_rows(d, rows);
    double x0 = DSB + 24, mw = c->w - DSB - 48, total = d->bytes ? (double)d->bytes : 1;
    if (y >= 118 && y <= 164) {
      for (int k = 0; k < nr; k++) {
        unsigned long long cnt = rows[k].free_ ? d->fr[rows[k].idx].count : d->part[rows[k].idx].count;
        double sx = x0 + du_bytes(d, rows[k].first) / total * mw, sw = du_bytes(d, cnt) / total * mw;
        if (x >= sx && x <= sx + (sw < 3 ? 3 : sw)) du_select(du_d, k);
      }
    } else if (y >= 204 && y < 372) {
      int k = (y - 204) / 24;
      if (k < nr) du_select(du_d, k);
    }
  }
  draw_own(c);
}

static void du_type(const char *buf, int n) {
  char *f = du_focus == 0 ? du_name : du_size;
  size_t cap = du_focus == 0 ? sizeof du_name : sizeof du_size;
  for (int i = 0; i < n; i++) {
    unsigned char ch = (unsigned char)buf[i];
    if (ch < 0x20 || ch > 0x7e || ch == '\'' || ch == '\\') continue; /* nur Einfaches: der Installer nimmt keine anderen Namen */
    if (strlen(f) + 1 < cap && strlen(f) < (du_focus == 0 ? 35u : 12u)) f[strlen(f) + 1] = 0, f[strlen(f)] = (char)ch;
  }
}

/* Tastatur: Pfeile wählen, Return führt aus, Esc bricht ab; a, e, s, Entf: die Werkzeuge */
static void du_key(Client *c, XKeyEvent *e) {
  char buf[16];
  KeySym ks = NoSymbol;
  int n = XLookupString(e, buf, sizeof buf, &ks, NULL);
  bool ret = ks == XK_Return || ks == XK_KP_Enter, esc = ks == XK_Escape;
  if (du_mode != DM_NONE) {
    bool text_mode = du_mode == DM_ADD || du_mode == DM_ERASE || du_mode == DM_RESIZE;
    if (esc) du_do(DA_CANCEL);
    else if (ret) du_do(DA_OK);
    else if (text_mode && ks == XK_Tab) { /* Felder: Name (0), Größe (1), Format (2) */
      static const int order_add[] = {0, 1, 2}, order_erase[] = {0, 2}, order_resize[] = {1};
      const int *o = du_mode == DM_ADD ? order_add : du_mode == DM_ERASE ? order_erase : order_resize;
      int cnt = du_mode == DM_ADD ? 3 : du_mode == DM_ERASE ? 2 : 1, at = 0;
      for (int i = 0; i < cnt; i++)
        if (o[i] == du_focus) at = i;
      du_focus = o[(at + 1) % cnt];
    } else if (text_mode && du_focus == 2 && (ks == XK_Left || ks == XK_Right)) {
      int cnt = du_mode == DM_ADD ? 3 : 2;
      du_fmt = (du_fmt + (ks == XK_Right ? 1 : cnt - 1)) % cnt;
    } else if (text_mode && du_focus != 2 && ks == XK_BackSpace) {
      char *f = du_focus == 0 ? du_name : du_size;
      if (*f) f[strlen(f) - 1] = 0;
    } else if (text_mode && du_focus != 2 && n > 0) {
      du_type(buf, n);
    }
  } else if (ndud && (ks == XK_Down || ks == XK_Up)) {
    /* durch Platten und Zeilen der Reihe nach */
    int idx[64][2], m = 0;
    for (int i = 0; i < ndud; i++) {
      idx[m][0] = i, idx[m++][1] = -1;
      DuRow rows[64];
      int nr = du_rows(&dud[i], rows);
      for (int k = 0; k < nr && m < 63; k++)
        if (!rows[k].free_) idx[m][0] = i, idx[m++][1] = k;
    }
    int cur = 0;
    for (int i = 0; i < m; i++)
      if (idx[i][0] == du_d && idx[i][1] == du_r) cur = i;
    cur = (cur + (ks == XK_Down ? 1 : m - 1)) % m;
    du_select(idx[cur][0], idx[cur][1]);
  } else if (ks == XK_a || ks == XK_A) du_do(DA_ADD);
  else if (ks == XK_e || ks == XK_E || ret) du_do(DA_ERASE);
  else if (ks == XK_s || ks == XK_S) du_do(DA_RESIZE);
  else if (ks == XK_Delete || ks == XK_BackSpace) du_do(DA_DELETE);
  else if (ks == XK_r || ks == XK_R) du_do(DA_REFRESH);
  draw_own(c);
}

static void show_disk_utility(long unused) {
  (void)unused;
  du_load();
  du_mode = DM_NONE;
  dmsg[0] = 0;
  own_window(DW, DH, "Disk Utility", du_paint, du_click, 0);
}
