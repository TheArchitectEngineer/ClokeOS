// libxlat-exec.dylib – lässt Programme der anderen Architektur "einfach laufen".
//
// Der PureDarwin-Kernel hat keinen Hook für fremde Binärdateien (execve -> EBADARCH, "bad CPU type in executable").
// Diese Bibliothek wird mit DYLD_INSERT_LIBRARIES in Shell/Desktop geladen und fängt execve, execv, execvp,
// posix_spawn und posix_spawnp ab: ist das Ziel ein Mach-O (dünn oder fat) ohne Scheibe für diese CPU, aber mit einer
// x86_64-Scheibe (auf arm64), wird stattdessen ucrun gestartet. Kinder eines übersetzten Programms behandelt
// ucrun selbst. Die Datei $XLAT_UCRUN (Standard /usr/bin/ucrun) ist der Übersetzer.
#include <errno.h>
#include <fcntl.h>
#include <spawn.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define INTERPOSE(rep, orig) __attribute__((used)) static struct { const void *r, *o; } _ip_##orig __attribute__((section("__DATA,__interpose"))) = {(const void *)(unsigned long)&rep, (const void *)(unsigned long)&orig}

extern char **environ;
#ifdef __arm64__
#define HOST_CPU 0x0100000c
#define FOREIGN_CPU 0x01000007
#else
#define HOST_CPU 0x01000007
#define FOREIGN_CPU 0x0100000c
#endif

static int foreign(const char *path) {
	uint8_t h[4096]; int fd = open(path, O_RDONLY); if (fd < 0) return 0;
	ssize_t n = read(fd, h, sizeof h); close(fd); if (n < 8) return 0;
	uint32_t m = *(uint32_t *)h;
	if (m == 0xfeedfacf) return *(uint32_t *)(h + 4) == FOREIGN_CPU;
	if (m == 0xbebafeca) {  // fat, Kopf big endian
		uint32_t na = __builtin_bswap32(*(uint32_t *)(h + 4)); int has_host = 0, has_foreign = 0;
		for (uint32_t i = 0; i < na && 8 + 20 * (i + 1) <= (size_t)n; i++) {
			uint32_t cpu = __builtin_bswap32(*(uint32_t *)(h + 8 + 20 * i));
			has_host |= cpu == HOST_CPU; has_foreign |= cpu == FOREIGN_CPU;
		}
		return has_foreign && !has_host;
	}
	return 0;
}

static char **wrap(const char *path, char *const argv[], const char **tr) {
	*tr = getenv("XLAT_UCRUN") ? getenv("XLAT_UCRUN") : "/usr/bin/ucrun";
	int n = 0; while (argv && argv[n]) n++;
	char **v = malloc((n + 3) * sizeof *v);
	v[0] = (char *)*tr; v[1] = (char *)path;
	for (int i = 1; i < n; i++) v[i + 1] = argv[i];
	v[n > 0 ? n + 1 : 2] = 0;
	return v;
}

static int my_execve(const char *path, char *const argv[], char *const envp[]) {
	const char *tr; if (foreign(path)) { char **v = wrap(path, argv, &tr); return execve(tr, v, envp); }
	return execve(path, argv, envp);
}
static int my_execv(const char *path, char *const argv[]) { return my_execve(path, argv, environ); }
static int my_execvp(const char *file, char *const argv[]) {
	if (strchr(file, '/')) return my_execv(file, argv);
	const char *p = getenv("PATH"); if (!p) p = "/usr/bin:/bin"; char buf[1024];
	while (*p) {
		const char *e = strchr(p, ':'); size_t l = e ? (size_t)(e - p) : strlen(p);
		if (l + strlen(file) + 2 < sizeof buf) { memcpy(buf, p, l); buf[l] = '/'; strcpy(buf + l + 1, file);
			if (foreign(buf)) return my_execv(buf, argv); }
		p += l + (e != 0);
	}
	return execvp(file, argv);
}
static int my_spawn(pid_t *pid, const char *path, const posix_spawn_file_actions_t *fa, const posix_spawnattr_t *at, char *const argv[], char *const envp[]) {
	const char *tr; if (foreign(path)) { char **v = wrap(path, argv, &tr); return posix_spawn(pid, tr, fa, at, v, envp); }
	return posix_spawn(pid, path, fa, at, argv, envp);
}
static int my_spawnp(pid_t *pid, const char *file, const posix_spawn_file_actions_t *fa, const posix_spawnattr_t *at, char *const argv[], char *const envp[]) {
	if (!strchr(file, '/')) {
		const char *p = getenv("PATH"); if (!p) p = "/usr/bin:/bin"; char buf[1024];
		while (*p) {
			const char *e = strchr(p, ':'); size_t l = e ? (size_t)(e - p) : strlen(p);
			if (l + strlen(file) + 2 < sizeof buf) { memcpy(buf, p, l); buf[l] = '/'; strcpy(buf + l + 1, file);
				if (foreign(buf)) return my_spawn(pid, buf, fa, at, argv, envp); }
			p += l + (e != 0);
		}
	} else if (foreign(file)) return my_spawn(pid, file, fa, at, argv, envp);
	return posix_spawnp(pid, file, fa, at, argv, envp);
}
INTERPOSE(my_execve, execve);
INTERPOSE(my_execv, execv);
INTERPOSE(my_execvp, execvp);
INTERPOSE(my_spawn, posix_spawn);
INTERPOSE(my_spawnp, posix_spawnp);
