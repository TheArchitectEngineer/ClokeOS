/* Sprache von ClokeOS. Wie bei macOS ist Englisch die Grundsprache: Im Code stehen die Texte
 * englisch, L() liefert sie in der eingestellten Sprache. Übersetzungen stehen in
 * strings-<sprache>.h als Paare {"English", "Übersetzung"}; fehlt ein Text dort, bleibt er
 * englisch.
 *
 * Eingestellt wird die Sprache systemweit in /etc/clokeos-language ("en" oder "de"), vom
 * Einrichtungsassistenten oder in den Systemeinstellungen unter „Sprache & Region“. Ohne die
 * Datei ist es Englisch. Zum Ausprobieren überschreibt CLOKE_LANG=de die Datei. */
#include "cloakserver.h"

#include <glib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static const char *const DE[][2] = {
#include "strings-de.h"
};

static char lang[8] = "en";
static GHashTable *table;

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
  snprintf(lang, sizeof lang, "%s", !strcmp(buf, "de") ? "de" : "en");
  if (table) g_hash_table_destroy(table);
  table = NULL;
  if (!strcmp(lang, "de")) {
    table = g_hash_table_new(g_str_hash, g_str_equal);
    for (size_t i = 0; i < sizeof DE / sizeof DE[0]; i++) g_hash_table_insert(table, (gpointer)DE[i][0], (gpointer)DE[i][1]);
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
const char *l10n_locale(void) { return !strcmp(lang, "de") ? "de_DE.UTF-8" : "en_US.UTF-8"; }

static const char *const WD_EN[] = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};
static const char *const WD_DE[] = {"Sonntag", "Montag", "Dienstag", "Mittwoch", "Donnerstag", "Freitag", "Samstag"};
static const char *const MO_EN[] = {"January", "February", "March", "April", "May", "June", "July", "August",
                                    "September", "October", "November", "December"};
static const char *const MO_DE[] = {"Januar", "Februar", "März", "April", "Mai", "Juni", "Juli", "August",
                                    "September", "Oktober", "November", "Dezember"};
static const char *const WDS_EN[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
static const char *const WDS_DE[] = {"So.", "Mo.", "Di.", "Mi.", "Do.", "Fr.", "Sa."};
static const char *const MOS_EN[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
static const char *const MOS_DE[] = {"Jan.", "Feb.", "März", "Apr.", "Mai", "Juni", "Juli", "Aug.", "Sept.", "Okt.",
                                     "Nov.", "Dez."};

const char *l10n_month(int m) { return (!strcmp(lang, "de") ? MO_DE : MO_EN)[m % 12]; }
const char *l10n_month_short(int m) { return (!strcmp(lang, "de") ? MOS_DE : MOS_EN)[m % 12]; }

/* "Friday, October 23" / "Freitag, 23. Oktober" */
void l10n_date_long(char *out, size_t n, const struct tm *t) {
  if (!strcmp(lang, "de")) snprintf(out, n, "%s, %d. %s", WD_DE[t->tm_wday], t->tm_mday, MO_DE[t->tm_mon]);
  else snprintf(out, n, "%s, %s %d", WD_EN[t->tm_wday], MO_EN[t->tm_mon], t->tm_mday);
}

/* Uhr der Menüleiste wie bei macOS: "Fri Oct 23  3:22 PM" / "Fr. 23. Okt. 15:22" */
void l10n_menubar_clock(char *out, size_t n, const struct tm *t) {
  if (!strcmp(lang, "de")) {
    snprintf(out, n, "%s %d. %s  %02d:%02d", WDS_DE[t->tm_wday], t->tm_mday, MOS_DE[t->tm_mon], t->tm_hour, t->tm_min);
  } else {
    int h = t->tm_hour % 12;
    snprintf(out, n, "%s %s %d  %d:%02d %s", WDS_EN[t->tm_wday], MOS_EN[t->tm_mon], t->tm_mday, h ? h : 12, t->tm_min,
             t->tm_hour < 12 ? "AM" : "PM");
  }
}

/* Große Uhr der Anmeldung: "3:41" / "15:41" (macOS zeigt dort ohne AM/PM) */
void l10n_big_clock(char *out, size_t n, const struct tm *t) {
  if (!strcmp(lang, "de")) {
    snprintf(out, n, "%02d:%02d", t->tm_hour, t->tm_min);
  } else {
    int h = t->tm_hour % 12;
    snprintf(out, n, "%d:%02d", h ? h : 12, t->tm_min);
  }
}
