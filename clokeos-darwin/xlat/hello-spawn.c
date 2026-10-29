// Dynamisches x86_64-Testprogramm für posix_spawn: Dateiaktionen (open/dup2/close), posix_spawnp, Fehler ENOENT, Rohr zum Kind.
#include <errno.h>
#include <fcntl.h>
#include <spawn.h>
#include <stdio.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>
extern char **environ;
static int fails;
#define CHECK(c, m) do { if (!(c)) { printf("FEHLER: %s\n", m); fails++; } } while (0)
int main(void) {
	pid_t pid; int st;
	// 1. Ausgabe des Kindes per Dateiaktion in eine Datei umleiten
	posix_spawn_file_actions_t fa; posix_spawn_file_actions_init(&fa);
	posix_spawn_file_actions_addopen(&fa, 1, "/tmp/spawn-test.out", O_WRONLY | O_CREAT | O_TRUNC, 0644);
	char *av1[] = {"echo", "hallo", "aus", "spawn", 0};
	int r = posix_spawn(&pid, "/bin/echo", &fa, 0, av1, environ); CHECK(r == 0, "posix_spawn echo");
	waitpid(pid, &st, 0); CHECK(WIFEXITED(st) && WEXITSTATUS(st) == 0, "Exit 0");
	char buf[100] = {0}; int fd = open("/tmp/spawn-test.out", O_RDONLY); read(fd, buf, 99); close(fd);
	CHECK(!strcmp(buf, "hallo aus spawn\n"), "umgeleitete Ausgabe");
	posix_spawn_file_actions_destroy(&fa);
	// 2. Rohr: Kind schreibt in die Röhre (dup2 + close), Eltern lesen
	int p[2]; pipe(p); posix_spawn_file_actions_init(&fa);
	posix_spawn_file_actions_adddup2(&fa, p[1], 1); posix_spawn_file_actions_addclose(&fa, p[0]); posix_spawn_file_actions_addclose(&fa, p[1]);
	char *av2[] = {"uname", "-m", 0};
	r = posix_spawnp(&pid, "uname", &fa, 0, av2, environ); CHECK(r == 0, "posix_spawnp uname (PATH)");
	close(p[1]); memset(buf, 0, sizeof buf); read(p[0], buf, 99); waitpid(pid, &st, 0);
	CHECK(!strncmp(buf, "x86_64", 6), "Ausgabe über Rohr: uname -m = x86_64");
	// 3. Programm fehlt
	r = posix_spawn(&pid, "/gibt/es/nicht", 0, 0, av1, environ); CHECK(r == ENOENT, "ENOENT");
	// 4. Exit-Code des Kindes
	char *av3[] = {"false", 0}; r = posix_spawnp(&pid, "false", 0, 0, av3, environ); if (r == 0) { waitpid(pid, &st, 0); CHECK(WIFEXITED(st) && WEXITSTATUS(st) == 1, "false -> 1"); }
	printf("hello-spawn: %s\n", fails ? "FEHLER" : "ok");
	return fails;
}
