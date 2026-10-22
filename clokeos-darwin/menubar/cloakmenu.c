/*
 * cloakmenu: holt die Menüleiste des aktiven Programms über D-Bus und gibt sie an CloakServer.
 *
 * GTK-Programme übergeben ihre Menüs wie bei macOS an die Leiste oben: entweder selbst
 * (GtkApplication) oder über appmenu-gtk-module, das die GtkMenuBar im Fenster ausblendet. Am
 * X-Fenster stehen dann _GTK_UNIQUE_BUS_NAME und die D-Bus-Pfade von Menü (org.gtk.Menus) und
 * Aktionen (org.gtk.Actions). Dieses Programm läuft als der angemeldete Benutzer auf dessen
 * Sitzungsbus (CloakServer selbst ist root) und spricht mit CloakServer zeilenweise über
 * stdin/stdout. Es belegt außerdem den Namen com.canonical.AppMenu.Registrar (siehe main).
 *
 *
 *   von CloakServer:  WATCH <xid> <bus> <menüpfad> <unity|-> <app|-> <win|->
 *                     CLEAR
 *                     ACTIVATE <nummer>
 *   an CloakServer:   MENU <xid>, dann je Eintrag eine Zeile, zuletzt END
 *
 * Eine Eintragszeile ist "<tiefe>\t<art>\t<nummer>\t<merkmale>\t<text>\t<kürzel>": art m =
 * Untermenü, i = Eintrag, s = Trennlinie; merkmale aus d (gesperrt), c (Haken), r (Auswahl).
 * Dasselbe Format liest CloakServer aus der Fenster-Eigenschaft _CLOKE_MENU (Mac-Programme).
 */
#include <gio/gio.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

typedef struct {
  const char *prefix;
  GActionGroup *group;
} Group;

typedef struct {
  char *action;
  GVariant *target;
} Act;

static GDBusConnection *bus;
static GMenuModel *root;
static Group groups[3] = {{"unity", NULL}, {"app", NULL}, {"win", NULL}};
static unsigned long xid;
static guint dump_id;
static GPtrArray *acts;      /* Nummer -> Act der letzten Ausgabe */
static GHashTable *watched;  /* Menümodelle mit items-changed-Beobachter */
static GPtrArray *handlers;  /* (Objekt, Handler) der Aktionsgruppen */

static void act_free(gpointer p) {
  Act *a = p;
  g_free(a->action);
  if (a->target) g_variant_unref(a->target);
  g_free(a);
}

static gboolean dump(gpointer unused);

static void schedule(void) {
  if (!dump_id) dump_id = g_timeout_add(40, dump, NULL);
}

static void on_items_changed(GMenuModel *m, gint pos, gint removed, gint added, gpointer u) { schedule(); }
static void on_action(GActionGroup *g, const char *name, gpointer u) { schedule(); }
static void on_action_bool(GActionGroup *g, const char *name, gboolean b, gpointer u) { schedule(); }
static void on_action_state(GActionGroup *g, const char *name, GVariant *v, gpointer u) { schedule(); }

static void watch_model(GMenuModel *m) {
  if (g_hash_table_contains(watched, m)) return;
  g_hash_table_add(watched, g_object_ref(m));
  g_signal_connect(m, "items-changed", G_CALLBACK(on_items_changed), NULL);
}

static GActionGroup *group_for(const char *action, const char **name) {
  const char *dot = strchr(action, '.');
  if (!dot) return NULL;
  for (int i = 0; i < 3; i++)
    if (groups[i].group && strlen(groups[i].prefix) == (size_t)(dot - action) &&
        !strncmp(groups[i].prefix, action, dot - action)) {
      *name = dot + 1;
      return groups[i].group;
    }
  return NULL;
}

/* "_Datei" -> "Datei", "__" -> "_"; Tabulatoren und Zeilenumbrüche stören das Zeilenformat */
static void clean_label(const char *in, char *out, size_t n) {
  size_t k = 0;
  for (const char *p = in; *p && k + 1 < n; p++) {
    if (*p == '_') {
      if (p[1] == '_') out[k++] = '_', p++;
      continue;
    }
    out[k++] = *p == '\t' || *p == '\n' || *p == '\r' ? ' ' : *p;
  }
  out[k] = 0;
}

/* "<Primary><Shift>z" -> "⇧⌃Z" wie in macOS-Menüs (⌃ ⌥ ⇧ ⌘, dann die Taste) */
static void accel_text(const char *accel, char *out, size_t n) {
  bool ctl = false, alt = false, shift = false, cmd = false;
  const char *p = accel;
  while (*p == '<') {
    const char *e = strchr(p, '>');
    if (!e) break;
    size_t l = e - p - 1;
    if (!strncmp(p + 1, "Primary", l) || !strncmp(p + 1, "Control", l) || !strncmp(p + 1, "Ctrl", l)) ctl = true;
    else if (!strncmp(p + 1, "Shift", l)) shift = true;
    else if (!strncmp(p + 1, "Alt", l) || !strncmp(p + 1, "Mod1", l)) alt = true;
    else if (!strncmp(p + 1, "Super", l) || !strncmp(p + 1, "Meta", l)) cmd = true;
    p = e + 1;
  }
  static const char *names[][2] = {
    {"Return", "↩"}, {"KP_Enter", "⌤"}, {"BackSpace", "⌫"}, {"Delete", "⌦"}, {"Escape", "⎋"}, {"Tab", "⇥"},
    {"space", "Leertaste"}, {"Left", "←"}, {"Right", "→"}, {"Up", "↑"}, {"Down", "↓"}, {"Page_Up", "⇞"},
    {"Page_Down", "⇟"}, {"Home", "↖"}, {"End", "↘"}, {"plus", "+"}, {"minus", "-"}, {"equal", "="},
    {"comma", ","}, {"period", "."}, {"slash", "/"}, {"backslash", "\\"}, {"question", "?"},
    {"KP_Add", "+"}, {"KP_Subtract", "-"}, {"KP_0", "0"}, {"Insert", "Einfg"}, {"Menu", "Menü"}};
  char key[32];
  if (!*p) {
    out[0] = 0;
    return;
  }
  snprintf(key, sizeof key, "%s", p);
  for (size_t i = 0; i < sizeof names / sizeof names[0]; i++)
    if (!strcmp(p, names[i][0])) snprintf(key, sizeof key, "%s", names[i][1]);
  if (!key[1]) key[0] = g_ascii_toupper(key[0]);
  snprintf(out, n, "%s%s%s%s%s", ctl ? "⌃" : "", alt ? "⌥" : "", shift ? "⇧" : "", cmd ? "⌘" : "", key);
}

/* Kürzel als Text, wie GTK es am Menüeintrag anzeigt ("Shift+Ctrl+N"), in dieselbe Schreibweise */
static void accel_label_text(const char *label, char *out, size_t n) {
  char buf[64];
  snprintf(buf, sizeof buf, "%s", label);
  bool ctl = false, alt = false, shift = false, cmd = false;
  char *key = buf, *plus;
  while ((plus = strchr(key, '+')) && plus[1]) {
    *plus = 0;
    if (!g_ascii_strcasecmp(key, "Ctrl") || !g_ascii_strcasecmp(key, "Control") || !g_ascii_strcasecmp(key, "Strg")) ctl = true;
    else if (!g_ascii_strcasecmp(key, "Shift") || !g_ascii_strcasecmp(key, "Umschalt")) shift = true;
    else if (!g_ascii_strcasecmp(key, "Alt")) alt = true;
    else if (!g_ascii_strcasecmp(key, "Super") || !g_ascii_strcasecmp(key, "Meta")) cmd = true;
    else {
      *plus = '+';
      break;
    }
    key = plus + 1;
  }
  char gtk[96];
  snprintf(gtk, sizeof gtk, "%s%s%s%s%s", ctl ? "<Primary>" : "", alt ? "<Alt>" : "", shift ? "<Shift>" : "",
           cmd ? "<Super>" : "", key);
  accel_text(gtk, out, n);
}

typedef struct {
  bool any;     /* auf dieser Ebene schon etwas ausgegeben */
  bool pending; /* Trennlinie vor dem nächsten Eintrag */
} Level;

static void emit(GString *o, int depth, char kind, unsigned id, const char *flags, const char *label, const char *accel) {
  g_string_append_printf(o, "%d\t%c\t%u\t%s\t%s\t%s\n", depth, kind, id, flags, label, accel);
}

static void dump_model(GString *o, GMenuModel *m, int depth, Level *lv) {
  watch_model(m);
  int n = g_menu_model_get_n_items(m); /* lädt das Modell nach und nach, items-changed folgt */
  for (int i = 0; i < n; i++) {
    GMenuModel *sec = g_menu_model_get_item_link(m, i, G_MENU_LINK_SECTION);
    if (sec) {
      if (lv->any) lv->pending = true;
      dump_model(o, sec, depth, lv);
      if (lv->any) lv->pending = true;
      g_object_unref(sec);
      continue;
    }
    char *label = NULL, *action = NULL, *accel = NULL, *accel_label = NULL, *hidden = NULL;
    g_menu_model_get_item_attribute(m, i, G_MENU_ATTRIBUTE_LABEL, "s", &label);
    g_menu_model_get_item_attribute(m, i, G_MENU_ATTRIBUTE_ACTION, "s", &action);
    g_menu_model_get_item_attribute(m, i, "accel", "s", &accel);
    g_menu_model_get_item_attribute(m, i, "x-canonical-accel", "s", &accel_label);
    g_menu_model_get_item_attribute(m, i, "hidden-when", "s", &hidden);
    GVariant *target = g_menu_model_get_item_attribute_value(m, i, G_MENU_ATTRIBUTE_TARGET, NULL);
    GMenuModel *sub = g_menu_model_get_item_link(m, i, G_MENU_LINK_SUBMENU);

    bool disabled = false, checked = false, radio = false, missing = false;
    if (action) {
      const char *name;
      GActionGroup *g = group_for(action, &name);
      gboolean enabled = FALSE;
      GVariant *state = NULL;
      if (g && g_action_group_query_action(g, name, &enabled, NULL, NULL, NULL, &state)) {
        disabled = !enabled;
        if (state && target) {
          radio = true;
          checked = g_variant_equal(state, target);
        } else if (state && g_variant_is_of_type(state, G_VARIANT_TYPE_BOOLEAN)) {
          checked = g_variant_get_boolean(state);
        }
      } else {
        missing = true;
        disabled = true;
      }
      if (state) g_variant_unref(state);
    } else if (!sub) {
      disabled = true;
    }
    bool hide = hidden && ((!strcmp(hidden, "action-disabled") && disabled) ||
                           (!strcmp(hidden, "action-missing") && missing) || !strcmp(hidden, "macos-menubar"));
    if (!hide && label) {
      if (lv->pending && lv->any) emit(o, depth, 's', 0, "", "", "");
      lv->pending = false;
      lv->any = true;
      char text[256], keys[64] = "", flags[4] = "";
      clean_label(label, text, sizeof text);
      if (accel) accel_text(accel, keys, sizeof keys);
      else if (accel_label) accel_label_text(accel_label, keys, sizeof keys);
      size_t f = 0;
      if (disabled && !sub) flags[f++] = 'd';
      if (checked) flags[f++] = 'c';
      if (radio) flags[f++] = 'r';
      unsigned id = 0;
      if (action && !sub) {
        Act *a = g_new0(Act, 1);
        a->action = g_strdup(action);
        a->target = target ? g_variant_ref(target) : NULL;
        g_ptr_array_add(acts, a);
        id = acts->len - 1;
      }
      emit(o, depth, sub ? 'm' : 'i', id, flags, text, keys);
      if (sub) {
        Level inner = {0};
        dump_model(o, sub, depth + 1, &inner);
      }
    }
    if (sub) g_object_unref(sub);
    if (target) g_variant_unref(target);
    g_free(label);
    g_free(action);
    g_free(accel);
    g_free(accel_label);
    g_free(hidden);
  }
}

static gboolean dump(gpointer unused) {
  dump_id = 0;
  g_ptr_array_set_size(acts, 0);
  GString *o = g_string_new(NULL);
  g_string_append_printf(o, "MENU %lu\n", root ? xid : 0);
  if (root) {
    Level lv = {0};
    dump_model(o, root, 0, &lv);
  }
  g_string_append(o, "END\n");
  fwrite(o->str, 1, o->len, stdout);
  fflush(stdout);
  g_string_free(o, TRUE);
  return G_SOURCE_REMOVE;
}

static void clear(void) {
  for (guint i = 0; i < handlers->len; i += 2)
    g_signal_handler_disconnect(handlers->pdata[i], GPOINTER_TO_UINT(handlers->pdata[i + 1]));
  g_ptr_array_set_size(handlers, 0);
  GHashTableIter it;
  gpointer m;
  g_hash_table_iter_init(&it, watched);
  while (g_hash_table_iter_next(&it, &m, NULL)) g_signal_handlers_disconnect_by_func(m, on_items_changed, NULL);
  g_hash_table_remove_all(watched);
  for (int i = 0; i < 3; i++) g_clear_object(&groups[i].group);
  g_clear_object(&root);
  g_ptr_array_set_size(acts, 0);
  xid = 0;
}

static void watch_group(int i, const char *busname, const char *path) {
  if (!strcmp(path, "-")) return;
  GActionGroup *g = G_ACTION_GROUP(g_dbus_action_group_get(bus, busname, path));
  groups[i].group = g;
  const char *sigs[] = {"action-added", "action-removed", "action-enabled-changed", "action-state-changed"};
  GCallback cbs[] = {G_CALLBACK(on_action), G_CALLBACK(on_action), G_CALLBACK(on_action_bool), G_CALLBACK(on_action_state)};
  for (int k = 0; k < 4; k++) {
    g_ptr_array_add(handlers, g);
    g_ptr_array_add(handlers, GUINT_TO_POINTER(g_signal_connect(g, sigs[k], cbs[k], NULL)));
  }
  g_strfreev(g_action_group_list_actions(g)); /* stößt das Laden der Aktionen an */
}

static void command(char *line) {
  char *argv[8] = {0};
  int argc = 0;
  for (char *t = strtok(line, " \t\r\n"); t && argc < 8; t = strtok(NULL, " \t\r\n")) argv[argc++] = t;
  if (argc == 0) return;
  if (!strcmp(argv[0], "WATCH") && argc == 7 && bus) {
    clear();
    xid = strtoul(argv[1], NULL, 0);
    root = G_MENU_MODEL(g_dbus_menu_model_get(bus, argv[2], argv[3]));
    watch_group(0, argv[2], argv[4]);
    watch_group(1, argv[2], argv[5]);
    watch_group(2, argv[2], argv[6]);
    schedule();
  } else if (!strcmp(argv[0], "CLEAR")) {
    clear();
    schedule();
  } else if (!strcmp(argv[0], "ACTIVATE") && argc == 2) {
    guint id = (guint)strtoul(argv[1], NULL, 10);
    if (id >= acts->len) return;
    Act *a = acts->pdata[id];
    const char *name;
    GActionGroup *g = group_for(a->action, &name);
    if (g) g_action_group_activate_action(g, name, a->target);
  }
}

static gboolean on_stdin(GIOChannel *ch, GIOCondition cond, gpointer loop) {
  char *line = NULL;
  gsize len;
  GIOStatus st = g_io_channel_read_line(ch, &line, &len, NULL, NULL);
  if (st == G_IO_STATUS_EOF || st == G_IO_STATUS_ERROR) {
    g_main_loop_quit(loop); /* CloakServer ist weg */
    return G_SOURCE_REMOVE;
  }
  if (line) command(line);
  g_free(line);
  return G_SOURCE_CONTINUE;
}

int main(void) {
  GError *err = NULL;
  bus = g_bus_get_sync(G_BUS_TYPE_SESSION, NULL, &err);
  if (!bus) fprintf(stderr, "cloakmenu: kein Sitzungsbus: %s\n", err ? err->message : "?");
  /* appmenu-gtk-module blendet die Menüleiste im Fenster nur aus, solange dieser Name auf dem Bus
   * belegt ist (bei GTK mit Wayland-Unterstützung, wie PureDarwins); sonst setzt es die
   * XSETTINGS-Einstellung Gtk/ShellShowsMenubar wieder zurück. Die Methoden des Registrars braucht
   * es nicht, nur den Namen. */
  if (bus)
    g_bus_own_name_on_connection(bus, "com.canonical.AppMenu.Registrar", G_BUS_NAME_OWNER_FLAGS_NONE, NULL, NULL, NULL,
                                 NULL);
  acts = g_ptr_array_new_with_free_func(act_free);
  watched = g_hash_table_new_full(g_direct_hash, g_direct_equal, g_object_unref, NULL);
  handlers = g_ptr_array_new();
  GMainLoop *loop = g_main_loop_new(NULL, FALSE);
  GIOChannel *ch = g_io_channel_unix_new(STDIN_FILENO);
  g_io_add_watch(ch, G_IO_IN | G_IO_HUP | G_IO_ERR, on_stdin, loop);
  g_main_loop_run(loop);
  return 0;
}
