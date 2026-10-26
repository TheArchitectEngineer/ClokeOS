/*
 * clokeapps: die kleinen Programme aus dem Dock als echte Fenster statt im Terminal.
 *
 *   clokeapps notes | calc | calendar | activity
 *
 * Notizen (Liste links, Text rechts, speichert selbst), Rechner, Kalender (Monatsansicht) und
 * Aktivitätsanzeige (Prozessliste, alle 2 s neu). Aussehen und Maße wie in clokeos.html. Die Titelleiste
 * zeichnet CloakServer; die Fensterrolle ist die Bundle-Kennung, damit das Dock das Fenster dem
 * Programm zuordnet (cloakserver/bundles.py). Sprache nach /etc/clokeos-language.
 */
#include <glib/gstdio.h>
#include <gtk/gtk.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static gboolean de;
#define T(en, deu) (de ? (deu) : (en))

static const char *CSS =
    ".cl-side { background: @theme_bg_color; border-right: 1px solid alpha(currentColor, 0.12); }"
    ".cl-note-row { padding: 8px 12px; border-radius: 8px; margin: 2px 8px; }"
    ".cl-note-row:selected { background: #fbd34f; color: #2b2300; }"
    ".cl-note-title { font-weight: 600; }"
    ".cl-note-sub { font-size: 11px; opacity: 0.6; }"
    ".cl-text { font-size: 14px; padding: 18px 22px; }"
    ".cl-bar { padding: 6px 10px; border-bottom: 1px solid alpha(currentColor, 0.12); }"
    ".cl-calc { background: #1c1c1e; }"
    ".cl-calc-display { color: white; font-size: 44px; font-weight: 300; padding: 14px 16px 6px 16px; }"
    ".cl-calc button { border-radius: 999px; border: none; box-shadow: none; font-size: 20px; margin: 3px; padding: 0;"
    "  background: #333336; color: white; min-height: 52px; min-width: 52px; }"
    ".cl-calc button.fn { background: #a5a5a8; color: black; }"
    ".cl-calc button.op { background: #ff9f0a; color: white; }"
    ".cl-calc button.op.on { background: white; color: #ff9f0a; }"
    ".cl-calc button:active { opacity: 0.7; }"
    ".cl-month { font-size: 22px; font-weight: 700; }"
    ".cl-dow { font-size: 11px; font-weight: 600; opacity: 0.55; }"
    ".cl-day { font-size: 15px; padding: 8px 0; }"
    ".cl-day.out { opacity: 0.3; }"
    ".cl-day.today { background: #ff3b30; color: white; border-radius: 999px; font-weight: 700; }"
    ".cl-stat { font-size: 12px; opacity: 0.75; }";

static void load_css(void) {
  GtkCssProvider *p = gtk_css_provider_new();
  gtk_css_provider_load_from_data(p, CSS, -1, NULL);
  gtk_style_context_add_provider_for_screen(gdk_screen_get_default(), GTK_STYLE_PROVIDER(p),
                                            GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
}

static GtkWidget *new_window(const char *role, const char *title, int w, int h, gboolean fixed) {
  GtkWidget *win = gtk_window_new(GTK_WINDOW_TOPLEVEL);
  gtk_window_set_title(GTK_WINDOW(win), title);
  gtk_window_set_role(GTK_WINDOW(win), role);
  gtk_window_set_default_size(GTK_WINDOW(win), w, h);
  if (fixed) gtk_window_set_resizable(GTK_WINDOW(win), FALSE);
  g_signal_connect(win, "destroy", G_CALLBACK(gtk_main_quit), NULL);
  return win;
}

/* ---- Notizen ----------------------------------------------------------------------------------- */

typedef struct {
  GtkWidget *list, *view;
  GtkTextBuffer *buf;
  char *dir;
  char *cur;      /* Dateipfad der geöffneten Notiz */
  guint save_id;
  gboolean loading;
} Notes;

static char *first_line(const char *text) {
  const char *nl = strchr(text, '\n');
  char *l = nl ? g_strndup(text, (gsize)(nl - text)) : g_strdup(text);
  g_strstrip(l);
  if (!*l) {
    g_free(l);
    return g_strdup(T("New Note", "Neue Notiz"));
  }
  return l;
}

static void notes_save(Notes *n) {
  if (!n->cur) return;
  GtkTextIter a, b;
  gtk_text_buffer_get_bounds(n->buf, &a, &b);
  char *t = gtk_text_buffer_get_text(n->buf, &a, &b, FALSE);
  g_file_set_contents(n->cur, t, -1, NULL);
  g_free(t);
}

static void notes_fill(Notes *n, const char *select);

static gboolean save_timeout(gpointer d) {
  Notes *n = d;
  n->save_id = 0;
  notes_save(n);
  notes_fill(n, n->cur);
  return G_SOURCE_REMOVE;
}

static void notes_changed(GtkTextBuffer *b, gpointer d) {
  (void)b;
  Notes *n = d;
  if (n->loading) return;
  if (n->save_id) g_source_remove(n->save_id);
  n->save_id = g_timeout_add(700, save_timeout, n);
}

static void notes_open(Notes *n, const char *path) {
  if (n->save_id) {
    g_source_remove(n->save_id);
    n->save_id = 0;
    notes_save(n);
  }
  char *t = NULL;
  g_file_get_contents(path, &t, NULL, NULL);
  n->loading = TRUE;
  gtk_text_buffer_set_text(n->buf, t ? t : "", -1);
  n->loading = FALSE;
  g_free(t);
  g_free(n->cur);
  n->cur = g_strdup(path);
}

static void on_row_selected(GtkListBox *l, GtkListBoxRow *r, gpointer d) {
  (void)l;
  Notes *n = d;
  if (!r) return;
  const char *path = g_object_get_data(G_OBJECT(r), "path");
  if (path && (!n->cur || strcmp(path, n->cur))) notes_open(n, path);
}

static gint newest_first(gconstpointer a, gconstpointer b) {
  GStatBuf sa, sb;
  g_stat(a, &sa);
  g_stat(b, &sb);
  return sa.st_mtime < sb.st_mtime ? 1 : sa.st_mtime > sb.st_mtime ? -1 : 0;
}

static void notes_fill(Notes *n, const char *select) {
  g_signal_handlers_block_by_func(n->list, on_row_selected, n);
  GList *rows = gtk_container_get_children(GTK_CONTAINER(n->list));
  for (GList *r = rows; r; r = r->next) gtk_widget_destroy(r->data);
  g_list_free(rows);
  GDir *d = g_dir_open(n->dir, 0, NULL);
  GList *files = NULL;
  for (const char *f; d && (f = g_dir_read_name(d));)
    if (g_str_has_suffix(f, ".txt")) files = g_list_prepend(files, g_build_filename(n->dir, f, NULL));
  if (d) g_dir_close(d);
  files = g_list_sort(files, newest_first);
  GtkWidget *sel = NULL;
  for (GList *f = files; f; f = f->next) {
    char *t = NULL;
    g_file_get_contents(f->data, &t, NULL, NULL);
    char *title = first_line(t ? t : "");
    GStatBuf st;
    g_stat(f->data, &st);
    GDateTime *dt = g_date_time_new_from_unix_local(st.st_mtime);
    char *when = g_date_time_format(dt, "%d.%m.%Y %H:%M");
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    GtkWidget *l1 = gtk_label_new(title), *l2 = gtk_label_new(when);
    gtk_label_set_xalign(GTK_LABEL(l1), 0);
    gtk_label_set_xalign(GTK_LABEL(l2), 0);
    gtk_label_set_ellipsize(GTK_LABEL(l1), PANGO_ELLIPSIZE_END);
    gtk_style_context_add_class(gtk_widget_get_style_context(l1), "cl-note-title");
    gtk_style_context_add_class(gtk_widget_get_style_context(l2), "cl-note-sub");
    gtk_box_pack_start(GTK_BOX(box), l1, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), l2, FALSE, FALSE, 0);
    GtkWidget *row = gtk_list_box_row_new();
    gtk_style_context_add_class(gtk_widget_get_style_context(row), "cl-note-row");
    gtk_container_add(GTK_CONTAINER(row), box);
    g_object_set_data_full(G_OBJECT(row), "path", g_strdup(f->data), g_free);
    gtk_list_box_insert(GTK_LIST_BOX(n->list), row, -1);
    if (select && !strcmp(select, f->data)) sel = row;
    g_free(t);
    g_free(title);
    g_free(when);
    g_date_time_unref(dt);
  }
  g_list_free_full(files, g_free);
  gtk_widget_show_all(n->list);
  if (sel) gtk_list_box_select_row(GTK_LIST_BOX(n->list), GTK_LIST_BOX_ROW(sel));
  g_signal_handlers_unblock_by_func(n->list, on_row_selected, n);
}

static void notes_new(GtkButton *b, gpointer d) {
  (void)b;
  Notes *n = d;
  notes_save(n);
  char *name = g_strdup_printf("note-%ld.txt", (long)time(NULL));
  char *path = g_build_filename(n->dir, name, NULL);
  g_file_set_contents(path, "", 0, NULL);
  notes_open(n, path);
  notes_fill(n, path);
  gtk_widget_grab_focus(n->view);
  g_free(name);
  g_free(path);
}

static void notes_delete(GtkButton *b, gpointer d) {
  (void)b;
  Notes *n = d;
  if (!n->cur) return;
  if (n->save_id) g_source_remove(n->save_id), n->save_id = 0;
  g_remove(n->cur);
  g_free(n->cur);
  n->cur = NULL;
  n->loading = TRUE;
  gtk_text_buffer_set_text(n->buf, "", -1);
  n->loading = FALSE;
  notes_fill(n, NULL);
  GtkListBoxRow *first = gtk_list_box_get_row_at_index(GTK_LIST_BOX(n->list), 0);
  if (first) gtk_list_box_select_row(GTK_LIST_BOX(n->list), first);
}

/* Textknopf statt Symbol: PureDarwins Symbolsatz hat nicht alle Zeichen */
static GtkWidget *icon_button(const char *label, const char *tip) {
  GtkWidget *b = gtk_button_new_with_label(label);
  gtk_widget_set_tooltip_text(b, tip);
  gtk_button_set_relief(GTK_BUTTON(b), GTK_RELIEF_NONE);
  return b;
}

static void run_notes(void) {
  Notes *n = g_new0(Notes, 1);
  n->dir = g_build_filename(g_get_home_dir(), "Notes", NULL);
  g_mkdir_with_parents(n->dir, 0755);
  /* die alte einzelne Datei aus der Terminal-Version übernehmen */
  char *old = g_build_filename(g_get_home_dir(), "Notes.txt", NULL);
  char *old_t = NULL;
  if (g_file_get_contents(old, &old_t, NULL, NULL) && *old_t) {
    char *p = g_build_filename(n->dir, "note-imported.txt", NULL);
    if (!g_file_test(p, G_FILE_TEST_EXISTS)) g_file_set_contents(p, old_t, -1, NULL);
    g_free(p);
    g_remove(old);
  }
  g_free(old_t);
  g_free(old);

  GtkWidget *win = new_window("com.clokeos.notizen", T("Notes", "Notizen"), 740, 470, FALSE);
  GtkWidget *hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
  GtkWidget *side = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
  gtk_style_context_add_class(gtk_widget_get_style_context(side), "cl-side");
  gtk_widget_set_size_request(side, 230, -1);
  GtkWidget *bar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
  gtk_style_context_add_class(gtk_widget_get_style_context(bar), "cl-bar");
  GtkWidget *bnew = icon_button("＋", T("New note", "Neue Notiz"));
  GtkWidget *bdel = icon_button(T("Delete", "Löschen"), T("Delete note", "Notiz löschen"));
  gtk_box_pack_start(GTK_BOX(bar), bnew, FALSE, FALSE, 0);
  gtk_box_pack_end(GTK_BOX(bar), bdel, FALSE, FALSE, 0);
  n->list = gtk_list_box_new();
  GtkWidget *sc = gtk_scrolled_window_new(NULL, NULL);
  gtk_container_add(GTK_CONTAINER(sc), n->list);
  gtk_box_pack_start(GTK_BOX(side), bar, FALSE, FALSE, 0);
  gtk_box_pack_start(GTK_BOX(side), sc, TRUE, TRUE, 0);
  n->view = gtk_text_view_new();
  gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(n->view), GTK_WRAP_WORD_CHAR);
  gtk_style_context_add_class(gtk_widget_get_style_context(n->view), "cl-text");
  n->buf = gtk_text_view_get_buffer(GTK_TEXT_VIEW(n->view));
  GtkWidget *sc2 = gtk_scrolled_window_new(NULL, NULL);
  gtk_container_add(GTK_CONTAINER(sc2), n->view);
  gtk_box_pack_start(GTK_BOX(hbox), side, FALSE, FALSE, 0);
  gtk_box_pack_start(GTK_BOX(hbox), sc2, TRUE, TRUE, 0);
  gtk_container_add(GTK_CONTAINER(win), hbox);

  g_signal_connect(n->list, "row-selected", G_CALLBACK(on_row_selected), n);
  g_signal_connect(n->buf, "changed", G_CALLBACK(notes_changed), n);
  g_signal_connect(bnew, "clicked", G_CALLBACK(notes_new), n);
  g_signal_connect(bdel, "clicked", G_CALLBACK(notes_delete), n);

  notes_fill(n, NULL);
  GtkListBoxRow *first = gtk_list_box_get_row_at_index(GTK_LIST_BOX(n->list), 0);
  if (first) gtk_list_box_select_row(GTK_LIST_BOX(n->list), first);
  else notes_new(NULL, n);
  gtk_widget_show_all(win);
  gtk_main();
  notes_save(n);
}

/* ---- Rechner ------------------------------------------------------------------------------------ */

typedef struct {
  GtkWidget *disp;
  double acc, cur;
  char op;           /* + - * / oder 0 */
  gboolean typing;   /* cur wird gerade eingetippt */
  char buf[32];
  GtkWidget *opbtn[4];
} Calc;

static void calc_show(Calc *c, double v) {
  char t[48];
  if (isnan(v) || isinf(v)) snprintf(t, sizeof t, "%s", T("Error", "Fehler"));
  else if (fabs(v) >= 1e12 || (v != 0 && fabs(v) < 1e-9)) snprintf(t, sizeof t, "%.6g", v);
  else {
    snprintf(t, sizeof t, "%.10f", v);
    char *e = t + strlen(t) - 1;
    while (*e == '0') *e-- = 0;
    if (*e == '.') *e = 0;
  }
  if (de) {
    char *dot = strchr(t, '.');
    if (dot) *dot = ',';
  }
  gtk_label_set_text(GTK_LABEL(c->disp), t);
}

static double apply(double a, char op, double b) {
  switch (op) {
    case '+': return a + b;
    case '-': return a - b;
    case '*': return a * b;
    case '/': return b == 0 ? NAN : a / b;
  }
  return b;
}

static void calc_mark(Calc *c, char op) {
  const char ops[4] = {'/', '*', '-', '+'};
  for (int i = 0; i < 4; i++) {
    GtkStyleContext *sc = gtk_widget_get_style_context(c->opbtn[i]);
    if (op == ops[i]) gtk_style_context_add_class(sc, "on");
    else gtk_style_context_remove_class(sc, "on");
  }
}

static void calc_key(GtkButton *b, gpointer d) {
  Calc *c = d;
  const char *k = g_object_get_data(G_OBJECT(b), "k");
  if ((*k >= '0' && *k <= '9') || *k == '.') {
    if (!c->typing) {
      c->buf[0] = 0;
      c->typing = TRUE;
    }
    size_t l = strlen(c->buf);
    if (*k == '.' && strchr(c->buf, '.')) return;
    if (l < 14) {
      if (*k == '.' && !l) strcpy(c->buf, "0");
      strncat(c->buf, k, 1);
    }
    if (!strcmp(c->buf, "0") || !strcmp(c->buf, "00")) strcpy(c->buf, "0");
    if (c->buf[0] == '0' && c->buf[1] >= '0' && c->buf[1] <= '9') memmove(c->buf, c->buf + 1, strlen(c->buf));
    c->cur = atof(c->buf);
    char t[40];
    snprintf(t, sizeof t, "%s", c->buf);
    if (de) {
      char *dot = strchr(t, '.');
      if (dot) *dot = ',';
    }
    gtk_label_set_text(GTK_LABEL(c->disp), t);
    calc_mark(c, 0);
  } else if (!strcmp(k, "AC")) {
    c->acc = c->cur = 0;
    c->op = 0;
    c->typing = FALSE;
    calc_show(c, 0);
    calc_mark(c, 0);
  } else if (!strcmp(k, "±")) {
    c->cur = -c->cur;
    c->typing = FALSE;
    calc_show(c, c->cur);
  } else if (!strcmp(k, "%")) {
    c->cur /= 100;
    c->typing = FALSE;
    calc_show(c, c->cur);
  } else {
    char op = !strcmp(k, "÷") ? '/' : !strcmp(k, "×") ? '*' : !strcmp(k, "−") ? '-' : !strcmp(k, "+") ? '+' : '=';
    if (c->op && (c->typing || op == '=')) c->acc = apply(c->acc, c->op, c->cur);
    else if (!c->op) c->acc = c->cur;
    c->cur = c->acc;
    calc_show(c, c->acc);
    c->typing = FALSE;
    c->op = op == '=' ? 0 : op;
    calc_mark(c, c->op);
  }
}

static void run_calc(void) {
  Calc *c = g_new0(Calc, 1);
  GtkWidget *win = new_window("com.clokeos.rechner", T("Calculator", "Rechner"), 252, 400, TRUE);
  GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
  gtk_style_context_add_class(gtk_widget_get_style_context(box), "cl-calc");
  c->disp = gtk_label_new("0");
  gtk_label_set_xalign(GTK_LABEL(c->disp), 1);
  gtk_style_context_add_class(gtk_widget_get_style_context(c->disp), "cl-calc-display");
  gtk_box_pack_start(GTK_BOX(box), c->disp, TRUE, TRUE, 0);
  GtkWidget *grid = gtk_grid_new();
  gtk_grid_set_column_homogeneous(GTK_GRID(grid), TRUE);
  gtk_widget_set_margin_start(grid, 6);
  gtk_widget_set_margin_end(grid, 6);
  gtk_widget_set_margin_bottom(grid, 8);
  static const char *keys[5][4] = {{"AC", "±", "%", "÷"}, {"7", "8", "9", "×"}, {"4", "5", "6", "−"},
                                   {"1", "2", "3", "+"}, {"0", ".", "=", NULL}};
  for (int r = 0; r < 5; r++)
    for (int col = 0; col < 4; col++) {
      const char *k = keys[r][col];
      if (!k) continue;
      const char *label = !strcmp(k, ".") ? T(".", ",") : k;
      GtkWidget *b = gtk_button_new_with_label(label);
      g_object_set_data(G_OBJECT(b), "k", (gpointer)k);
      GtkStyleContext *sc = gtk_widget_get_style_context(b);
      if (r == 0 && col < 3) gtk_style_context_add_class(sc, "fn");
      if (col == 3 || !strcmp(k, "=")) gtk_style_context_add_class(sc, "op");
      if (col == 3 && r < 4) c->opbtn[r == 0 ? 0 : r == 1 ? 1 : r == 2 ? 2 : 3] = b;
      if (!strcmp(k, "=")) gtk_style_context_add_class(sc, "op");
      g_signal_connect(b, "clicked", G_CALLBACK(calc_key), c);
      gtk_grid_attach(GTK_GRID(grid), b, col, r, 1, 1);
    }
  /* 0 über zwei Spalten wie beim Vorbild: "0" und "." bleiben einzeln, = rechts daneben */
  gtk_box_pack_start(GTK_BOX(box), grid, FALSE, FALSE, 0);
  gtk_container_add(GTK_CONTAINER(win), box);
  gtk_widget_show_all(win);
  gtk_main();
}

/* ---- Kalender ------------------------------------------------------------------------------------- */

typedef struct {
  GtkWidget *title, *grid;
  int y, m;   /* angezeigter Monat */
} Cal;

static void cal_fill(Cal *c);

static void cal_step(GtkButton *b, gpointer d) {
  Cal *c = d;
  int dm = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(b), "dm"));
  if (!dm) {
    GDateTime *now = g_date_time_new_now_local();
    c->y = g_date_time_get_year(now);
    c->m = g_date_time_get_month(now);
    g_date_time_unref(now);
  } else {
    c->m += dm;
    if (c->m < 1) c->m = 12, c->y--;
    if (c->m > 12) c->m = 1, c->y++;
  }
  cal_fill(c);
}

static void cal_fill(Cal *c) {
  static const char *mde[] = {"Januar", "Februar", "März", "April", "Mai", "Juni", "Juli", "August", "September", "Oktober", "November", "Dezember"};
  static const char *men[] = {"January", "February", "March", "April", "May", "June", "July", "August", "September", "October", "November", "December"};
  char t[64];
  snprintf(t, sizeof t, "%s %d", (de ? mde : men)[c->m - 1], c->y);
  gtk_label_set_text(GTK_LABEL(c->title), t);
  GList *kids = gtk_container_get_children(GTK_CONTAINER(c->grid));
  for (GList *k = kids; k; k = k->next) gtk_widget_destroy(k->data);
  g_list_free(kids);
  static const char *dde[] = {"Mo", "Di", "Mi", "Do", "Fr", "Sa", "So"};
  static const char *den[] = {"Su", "Mo", "Tu", "We", "Th", "Fr", "Sa"};
  for (int i = 0; i < 7; i++) {
    GtkWidget *l = gtk_label_new(de ? dde[i] : den[i]);
    gtk_style_context_add_class(gtk_widget_get_style_context(l), "cl-dow");
    gtk_grid_attach(GTK_GRID(c->grid), l, i, 0, 1, 1);
  }
  GDateTime *first = g_date_time_new_local(c->y, c->m, 1, 0, 0, 0);
  int wd = g_date_time_get_day_of_week(first);          /* 1 = Montag … 7 = Sonntag */
  int lead = de ? wd - 1 : wd % 7;                      /* Tage des Vormonats vor dem 1. */
  GDateTime *start = g_date_time_add_days(first, -lead);
  GDateTime *now = g_date_time_new_now_local();
  for (int i = 0; i < 42; i++) {
    GDateTime *day = g_date_time_add_days(start, i);
    char n[8];
    snprintf(n, sizeof n, "%d", g_date_time_get_day_of_month(day));
    GtkWidget *l = gtk_label_new(n);
    GtkStyleContext *sc = gtk_widget_get_style_context(l);
    gtk_style_context_add_class(sc, "cl-day");
    if (g_date_time_get_month(day) != c->m) gtk_style_context_add_class(sc, "out");
    if (g_date_time_get_year(day) == g_date_time_get_year(now) && g_date_time_get_month(day) == g_date_time_get_month(now) &&
        g_date_time_get_day_of_month(day) == g_date_time_get_day_of_month(now))
      gtk_style_context_add_class(sc, "today");
    gtk_widget_set_halign(l, GTK_ALIGN_CENTER);
    gtk_widget_set_size_request(l, 36, 36);
    gtk_grid_attach(GTK_GRID(c->grid), l, i % 7, 1 + i / 7, 1, 1);
    g_date_time_unref(day);
  }
  g_date_time_unref(now);
  g_date_time_unref(start);
  g_date_time_unref(first);
  gtk_widget_show_all(c->grid);
}

static void run_calendar(void) {
  Cal *c = g_new0(Cal, 1);
  GtkWidget *win = new_window("com.clokeos.kalender", T("Calendar", "Kalender"), 760, 520, FALSE);
  GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
  gtk_container_set_border_width(GTK_CONTAINER(box), 22);
  GtkWidget *bar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
  c->title = gtk_label_new("");
  gtk_style_context_add_class(gtk_widget_get_style_context(c->title), "cl-month");
  GtkWidget *prev = icon_button("‹", T("Previous month", "Voriger Monat"));
  GtkWidget *next = icon_button("›", T("Next month", "Nächster Monat"));
  GtkWidget *today = gtk_button_new_with_label(T("Today", "Heute"));
  g_object_set_data(G_OBJECT(prev), "dm", GINT_TO_POINTER(-1));
  g_object_set_data(G_OBJECT(next), "dm", GINT_TO_POINTER(1));
  g_object_set_data(G_OBJECT(today), "dm", GINT_TO_POINTER(0));
  g_signal_connect(prev, "clicked", G_CALLBACK(cal_step), c);
  g_signal_connect(next, "clicked", G_CALLBACK(cal_step), c);
  g_signal_connect(today, "clicked", G_CALLBACK(cal_step), c);
  gtk_box_pack_start(GTK_BOX(bar), c->title, FALSE, FALSE, 0);
  gtk_box_pack_end(GTK_BOX(bar), next, FALSE, FALSE, 0);
  gtk_box_pack_end(GTK_BOX(bar), today, FALSE, FALSE, 0);
  gtk_box_pack_end(GTK_BOX(bar), prev, FALSE, FALSE, 0);
  c->grid = gtk_grid_new();
  gtk_grid_set_column_homogeneous(GTK_GRID(c->grid), TRUE);
  gtk_grid_set_row_spacing(GTK_GRID(c->grid), 4);
  gtk_widget_set_vexpand(c->grid, TRUE);
  gtk_box_pack_start(GTK_BOX(box), bar, FALSE, FALSE, 0);
  gtk_box_pack_start(GTK_BOX(box), c->grid, TRUE, TRUE, 0);
  gtk_container_add(GTK_CONTAINER(win), box);
  cal_step(GTK_BUTTON(today), c);
  gtk_widget_show_all(win);
  gtk_main();
}

/* ---- Aktivitätsanzeige ------------------------------------------------------------------------------ */

enum { A_PID, A_NAME, A_CPU, A_MEM, A_COLS };

typedef struct {
  GtkListStore *store;
  GtkWidget *stat;
} Act;

static gboolean act_refresh(gpointer d) {
  Act *a = d;
  gchar *out = NULL;
  if (!g_spawn_command_line_sync("/bin/ps -axo pid=,pcpu=,rss=,comm=", &out, NULL, NULL, NULL) || !out) return G_SOURCE_CONTINUE;
  gtk_list_store_clear(a->store);
  double total = 0;
  int count = 0;
  gchar **lines = g_strsplit(out, "\n", -1);
  for (gchar **l = lines; *l; l++) {
    int pid = 0, off = 0;
    double cpu = 0;
    long rss = 0;
    if (sscanf(*l, " %d %lf %ld %n", &pid, &cpu, &rss, &off) < 3 || !off) continue;
    const char *name = strrchr(*l + off, '/');
    name = name ? name + 1 : *l + off;
    GtkTreeIter it;
    gtk_list_store_append(a->store, &it);
    gtk_list_store_set(a->store, &it, A_PID, pid, A_NAME, name, A_CPU, cpu, A_MEM, rss / 1024.0, -1);
    total += cpu;
    count++;
  }
  g_strfreev(lines);
  g_free(out);
  double load[3] = {0};
  getloadavg(load, 3);
  char t[160];
  snprintf(t, sizeof t, "%s %.1f %%   ·   %d %s   ·   %s %.2f", T("CPU", "CPU"), total, count, T("processes", "Prozesse"),
           T("Load", "Last"), load[0]);
  gtk_label_set_text(GTK_LABEL(a->stat), t);
  return G_SOURCE_CONTINUE;
}

static void cell_num(GtkTreeViewColumn *c, GtkCellRenderer *r, GtkTreeModel *m, GtkTreeIter *i, gpointer col) {
  (void)c;
  double v;
  gtk_tree_model_get(m, i, GPOINTER_TO_INT(col), &v, -1);
  char t[32];
  if (GPOINTER_TO_INT(col) == A_CPU) snprintf(t, sizeof t, "%.1f", v);
  else snprintf(t, sizeof t, "%.1f MB", v);
  g_object_set(r, "text", t, "xalign", 1.0f, NULL);
}

static void run_activity(void) {
  Act *a = g_new0(Act, 1);
  GtkWidget *win = new_window("com.clokeos.aktivitaet", T("Activity Monitor", "Aktivitätsanzeige"), 680, 460, FALSE);
  GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
  a->stat = gtk_label_new("");
  gtk_label_set_xalign(GTK_LABEL(a->stat), 0);
  gtk_style_context_add_class(gtk_widget_get_style_context(a->stat), "cl-stat");
  gtk_style_context_add_class(gtk_widget_get_style_context(a->stat), "cl-bar");
  a->store = gtk_list_store_new(A_COLS, G_TYPE_INT, G_TYPE_STRING, G_TYPE_DOUBLE, G_TYPE_DOUBLE);
  gtk_tree_sortable_set_sort_column_id(GTK_TREE_SORTABLE(a->store), A_CPU, GTK_SORT_DESCENDING);
  GtkWidget *tv = gtk_tree_view_new_with_model(GTK_TREE_MODEL(a->store));
  gtk_tree_view_set_headers_clickable(GTK_TREE_VIEW(tv), TRUE);
  const char *heads_en[] = {"PID", "Process Name", "% CPU", "Memory"}, *heads_de[] = {"PID", "Prozessname", "% CPU", "Speicher"};
  for (int i = 0; i < A_COLS; i++) {
    GtkCellRenderer *r = gtk_cell_renderer_text_new();
    GtkTreeViewColumn *col = gtk_tree_view_column_new_with_attributes((de ? heads_de : heads_en)[i], r, NULL);
    if (i == A_PID) gtk_tree_view_column_add_attribute(col, r, "text", A_PID);
    else if (i == A_NAME) gtk_tree_view_column_add_attribute(col, r, "text", A_NAME);
    else gtk_tree_view_column_set_cell_data_func(col, r, cell_num, GINT_TO_POINTER(i), NULL);
    gtk_tree_view_column_set_sort_column_id(col, i);
    gtk_tree_view_column_set_resizable(col, TRUE);
    if (i == A_NAME) gtk_tree_view_column_set_expand(col, TRUE);
    gtk_tree_view_append_column(GTK_TREE_VIEW(tv), col);
  }
  GtkWidget *sc = gtk_scrolled_window_new(NULL, NULL);
  gtk_container_add(GTK_CONTAINER(sc), tv);
  gtk_box_pack_start(GTK_BOX(box), a->stat, FALSE, FALSE, 0);
  gtk_box_pack_start(GTK_BOX(box), sc, TRUE, TRUE, 0);
  gtk_container_add(GTK_CONTAINER(win), box);
  act_refresh(a);
  g_timeout_add_seconds(2, act_refresh, a);
  gtk_widget_show_all(win);
  gtk_main();
}

int main(int argc, char **argv) {
  char *lang = NULL;
  de = g_file_get_contents("/etc/clokeos-language", &lang, NULL, NULL) && g_str_has_prefix(lang, "de");
  g_free(lang);
  if (argc < 2) {
    g_printerr("usage: clokeapps notes|calc|calendar|activity\n");
    return 2;
  }
  gtk_init(&argc, &argv);
  load_css();
  if (!strcmp(argv[1], "notes")) run_notes();
  else if (!strcmp(argv[1], "calc")) run_calc();
  else if (!strcmp(argv[1], "calendar")) run_calendar();
  else if (!strcmp(argv[1], "activity")) run_activity();
  else {
    g_printerr("unknown app %s\n", argv[1]);
    return 2;
  }
  return 0;
}
