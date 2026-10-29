// libclokeos-mime.dylib – Inhaltserkennung für Dateitypen in GIO-Programmen (Fundus/Thunar).
//
// Die glib des Images (neu, ab etwa 2.86) bestimmt den Typ einer lokalen Datei in g_file_query_info nur noch nach dem Namen
// und liest den Inhalt nie (get_content_type kennt nicht einmal mehr den Pfad). Programme, die den Typ aus dem Inhalt brauchen
// (ELF, x86_64-Mach-O, Skripte ohne Endung), sehen deshalb application/octet-stream. Diese Bibliothek wird mit
// DYLD_INSERT_LIBRARIES geladen und hängt sich an die Funktionen, mit denen Thunar Dateiinformationen holt
// (g_file_query_info, g_file_query_info_finish, g_file_enumerator_next_file(s)_finish): ist der Typ einer regulären Datei
// octet-stream, liest sie die ersten 4 KiB und fragt g_content_type_guess mit den Daten (die Datenbank dafür: mime/build.sh).
#include <fcntl.h>
#include <gio/gio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define INTERPOSE(rep, orig) __attribute__((used)) static struct { const void *r, *o; } _ip_##orig __attribute__((section("__DATA,__interpose"))) = {(const void *)(unsigned long)&rep, (const void *)(unsigned long)&orig}

static void fix_info(GFileInfo *info, const char *path) {
	if (!info || !path || !g_file_info_has_attribute(info, G_FILE_ATTRIBUTE_STANDARD_CONTENT_TYPE)) return;
	const char *ct = g_file_info_get_content_type(info);
	if (!ct || strcmp(ct, "application/octet-stream") != 0) return;
	struct stat st;
	if (stat(path, &st) != 0 || !S_ISREG(st.st_mode)) return;  // nur reguläre Dateien (kein standard::type nötig: nicht jeder Aufrufer fragt danach)
	int fd = open(path, O_RDONLY);
	if (fd < 0) return;
	guchar buf[4096]; ssize_t n = read(fd, buf, sizeof buf); close(fd);
	if (n <= 0) return;
	char *base = g_path_get_basename(path);
	char *t = g_content_type_guess(base, buf, (gsize)n, NULL);
	if (t && strcmp(t, "application/octet-stream") != 0) g_file_info_set_content_type(info, t);
	g_free(t); g_free(base);
}
static void fix_info_in_dir(GFileInfo *info, GFileEnumerator *en) {
	GFile *dir = en ? g_file_enumerator_get_container(en) : NULL;
	char *dp = dir ? g_file_get_path(dir) : NULL;
	const char *name = info ? g_file_info_get_name(info) : NULL;
	if (dp && name) { char *p = g_build_filename(dp, name, NULL); fix_info(info, p); g_free(p); }
	g_free(dp);
}

static GFileInfo *my_query_info(GFile *file, const char *attrs, GFileQueryInfoFlags flags, GCancellable *c, GError **err) {
	GFileInfo *info = g_file_query_info(file, attrs, flags, c, err);
	if (info) { char *p = g_file_get_path(file); fix_info(info, p); g_free(p); }
	return info;
}
static GFileInfo *my_query_info_finish(GFile *file, GAsyncResult *res, GError **err) {
	GFileInfo *info = g_file_query_info_finish(file, res, err);
	if (info) { char *p = g_file_get_path(file); fix_info(info, p); g_free(p); }
	return info;
}
static GFileInfo *my_next_file(GFileEnumerator *en, GCancellable *c, GError **err) {
	GFileInfo *info = g_file_enumerator_next_file(en, c, err);
	fix_info_in_dir(info, en);
	return info;
}
static GList *my_next_files_finish(GFileEnumerator *en, GAsyncResult *res, GError **err) {
	GList *l = g_file_enumerator_next_files_finish(en, res, err);
	for (GList *i = l; i; i = i->next) fix_info_in_dir(i->data, en);
	return l;
}
INTERPOSE(my_query_info, g_file_query_info);
INTERPOSE(my_query_info_finish, g_file_query_info_finish);
INTERPOSE(my_next_file, g_file_enumerator_next_file);
INTERPOSE(my_next_files_finish, g_file_enumerator_next_files_finish);
