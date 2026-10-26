/* Sprache von ClokeOS. Wie bei macOS ist Englisch die Grundsprache: Im Code stehen die Texte
 * englisch, L() liefert sie in der eingestellten Sprache. Übersetzungen stehen in
 * strings-<sprache>.h als Paare {"English", "Übersetzung"}; fehlt ein Text dort, bleibt er
 * englisch.
 *
 * Eingestellt wird die Sprache systemweit in /etc/clokeos-language ("en", "de", "fr", "es" oder "it"), vom
 * Einrichtungsassistenten oder in den Systemeinstellungen unter „Sprache & Region“. Ohne die
 * Datei ist es Englisch. Zum Ausprobieren überschreibt CLOKE_LANG=de die Datei. */
#include "cloakserver.h"

#include <glib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

typedef const char *const Pair[2];
static Pair DE[] = {
#include "strings-de.h"
};
static Pair FR[] = {
#include "strings-fr.h"
};
static Pair ES[] = {
#include "strings-es.h"
};
static Pair IT[] = {
#include "strings-it.h"
};

/* style: 0 US (12 h, "Friday, October 23"), 1 de ("Freitag, 23. Oktober"), 2 fr/it ("vendredi 23 octobre"),
 * 3 es ("viernes, 23 de octubre") */
typedef struct {
  const char *code, *name, *locale;
  Pair *pairs;
  size_t n;
  int style;
  const char *wd[7], *mo[12], *wds[7], *mos[12];
} Lang;

#define NP(a) (sizeof(a) / sizeof(a[0]))
static const Lang LANGS[] = {
    {"en", "English", "en_US.UTF-8", NULL, 0, 0,
     {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"},
     {"January", "February", "March", "April", "May", "June", "July", "August", "September", "October", "November", "December"},
     {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"},
     {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"}},
    {"de", "Deutsch", "de_DE.UTF-8", DE, NP(DE), 1,
     {"Sonntag", "Montag", "Dienstag", "Mittwoch", "Donnerstag", "Freitag", "Samstag"},
     {"Januar", "Februar", "März", "April", "Mai", "Juni", "Juli", "August", "September", "Oktober", "November", "Dezember"},
     {"So.", "Mo.", "Di.", "Mi.", "Do.", "Fr.", "Sa."},
     {"Jan.", "Feb.", "März", "Apr.", "Mai", "Juni", "Juli", "Aug.", "Sept.", "Okt.", "Nov.", "Dez."}},
    {"fr", "Français", "fr_FR.UTF-8", FR, NP(FR), 2,
     {"dimanche", "lundi", "mardi", "mercredi", "jeudi", "vendredi", "samedi"},
     {"janvier", "février", "mars", "avril", "mai", "juin", "juillet", "août", "septembre", "octobre", "novembre", "décembre"},
     {"dim.", "lun.", "mar.", "mer.", "jeu.", "ven.", "sam."},
     {"janv.", "févr.", "mars", "avr.", "mai", "juin", "juil.", "août", "sept.", "oct.", "nov.", "déc."}},
    {"es", "Español", "es_ES.UTF-8", ES, NP(ES), 3,
     {"domingo", "lunes", "martes", "miércoles", "jueves", "viernes", "sábado"},
     {"enero", "febrero", "marzo", "abril", "mayo", "junio", "julio", "agosto", "septiembre", "octubre", "noviembre", "diciembre"},
     {"dom", "lun", "mar", "mié", "jue", "vie", "sáb"},
     {"ene", "feb", "mar", "abr", "may", "jun", "jul", "ago", "sep", "oct", "nov", "dic"}},
    {"it", "Italiano", "it_IT.UTF-8", IT, NP(IT), 2,
     {"domenica", "lunedì", "martedì", "mercoledì", "giovedì", "venerdì", "sabato"},
     {"gennaio", "febbraio", "marzo", "aprile", "maggio", "giugno", "luglio", "agosto", "settembre", "ottobre", "novembre", "dicembre"},
     {"dom", "lun", "mar", "mer", "gio", "ven", "sab"},
     {"gen", "feb", "mar", "apr", "mag", "giu", "lug", "ago", "set", "ott", "nov", "dic"}},
};
#define NLANG ((int)NP(LANGS))

static char lang[8] = "en";
static const Lang *cur = &LANGS[0];
static GHashTable *table;

int l10n_count(void) { return NLANG; }
const char *l10n_code(int i) { return LANGS[i].code; }
const char *l10n_name(int i) { return LANGS[i].name; }

void l10n_init(void) {
  const char *env = getenv("CLOKE_LANG");
  char buf[16] = "";
  if (env && *env) {
    snprintf(buf, sizeof buf, "%s", env);
  } else {
    FILE *f = fopen(LANGUAGE_FILE, "r");
    if (f) {
      if (!fgets(buf, sizeof buf, f)) buf[0] = 0;
      fclose(f);
    }
  }
  buf[strcspn(buf, " \t\r\n_.-")] = 0;
  cur = &LANGS[0];
  for (int i = 0; i < NLANG; i++)
    if (!strcmp(buf, LANGS[i].code)) cur = &LANGS[i];
  snprintf(lang, sizeof lang, "%s", cur->code);
  if (table) g_hash_table_destroy(table);
  table = NULL;
  if (cur->pairs) {
    table = g_hash_table_new(g_str_hash, g_str_equal);
    for (size_t i = 0; i < cur->n; i++) g_hash_table_insert(table, (gpointer)cur->pairs[i][0], (gpointer)cur->pairs[i][1]);
  }
}

const char *L(const char *en) {
  if (!table || !en) return en;
  const char *t = g_hash_table_lookup(table, en);
  return t ? t : en;
}

const char *l10n_lang(void) { return lang; }

bool l10n_set_lang(const char *code) {
  FILE *f = fopen(LANGUAGE_FILE, "w");
  if (!f) return false;
  fprintf(f, "%s\n", code);
  fclose(f);
  unsetenv("CLOKE_LANG");
  l10n_init();
  return true;
}

/* Locale für die Programme der Sitzung (GTK übersetzt sich danach, soweit es Übersetzungen hat) */
const char *l10n_locale(void) { return cur->locale; }

const char *l10n_month(int m) { return cur->mo[m % 12]; }
const char *l10n_month_short(int m) { return cur->mos[m % 12]; }

/* "Friday, October 23" / "Freitag, 23. Oktober" / "vendredi 23 octobre" / "viernes, 23 de octubre" */
void l10n_date_long(char *out, size_t n, const struct tm *t) {
  const char *wd = cur->wd[t->tm_wday], *mo = cur->mo[t->tm_mon];
  switch (cur->style) {
  case 1: snprintf(out, n, "%s, %d. %s", wd, t->tm_mday, mo); break;
  case 2: snprintf(out, n, "%s %d %s", wd, t->tm_mday, mo); break;
  case 3: snprintf(out, n, "%s, %d de %s", wd, t->tm_mday, mo); break;
  default: snprintf(out, n, "%s, %s %d", wd, mo, t->tm_mday);
  }
}

/* Uhr der Menüleiste wie bei macOS: "Fri Oct 23  3:22 PM" / "Fr. 23. Okt. 15:22" */
void l10n_menubar_clock(char *out, size_t n, const struct tm *t) {
  const char *wd = cur->wds[t->tm_wday], *mo = cur->mos[t->tm_mon];
  switch (cur->style) {
  case 0: {
    int h = t->tm_hour % 12;
    snprintf(out, n, "%s %s %d  %d:%02d %s", wd, mo, t->tm_mday, h ? h : 12, t->tm_min, t->tm_hour < 12 ? "AM" : "PM");
    break;
  }
  case 1: snprintf(out, n, "%s %d. %s  %02d:%02d", wd, t->tm_mday, mo, t->tm_hour, t->tm_min); break;
  default: snprintf(out, n, "%s %d %s  %02d:%02d", wd, t->tm_mday, mo, t->tm_hour, t->tm_min);
  }
}

/* Große Uhr der Anmeldung: "3:41" / "15:41" (macOS zeigt dort ohne AM/PM) */
void l10n_big_clock(char *out, size_t n, const struct tm *t) {
  if (cur->style) {
    snprintf(out, n, "%02d:%02d", t->tm_hour, t->tm_min);
  } else {
    int h = t->tm_hour % 12;
    snprintf(out, n, "%d:%02d", h ? h : 12, t->tm_min);
  }
}

/* Stil der Uhr: 12 Stunden mit AM/PM nur im Englischen */
bool l10n_24h(void) { return cur->style != 0; }
