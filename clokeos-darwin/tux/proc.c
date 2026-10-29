// tux – /proc als Pfad-Umleitung: Beim Auflösen eines Pfads unter /proc legt tux die benötigten Dateien in
// /private/tmp/.tux-proc-<pid>/ an (frisch, mit den Werten dieses Moments) und leitet den Zugriff dorthin. So geht jedes
// open/stat/readdir normal. Es gibt /proc/self (und /proc/<eigene pid>) mit maps, status, stat, statm, cmdline, environ,
// comm, mounts, limits, fd/, cwd, exe, root sowie cpuinfo, meminfo, version, uptime, loadavg, stat, filesystems, mounts
// und ein paar sys/-Einträge. Andere Prozesse sieht man nicht.
#include "tux.h"
#include <dirent.h>
#include <errno.h>
#include <signal.h>
#include <fcntl.h>
#include <libproc.h>
#include <mach/mach.h>
#include <mach/mach_vm.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/sysctl.h>
#include <sys/time.h>
#include <unistd.h>

static char root[PATH_MAX];
static char *g_cmdline, *g_environ;
static size_t g_cmdlen, g_envlen;

static char *join_nul(char **v, size_t *len)
{
	size_t n = 0;
	for (char **p = v; p && *p; p++) n += strlen(*p) + 1;
	char *b = malloc(n + 1), *o = b;
	for (char **p = v; p && *p; p++) { size_t l = strlen(*p) + 1; memcpy(o, *p, l); o += l; }
	*len = n;
	return b;
}

void tux_proc_setargs(char **argv, char **envp)
{
	free(g_cmdline);
	free(g_environ);
	g_cmdline = join_nul(argv, &g_cmdlen);
	g_environ = join_nul(envp, &g_envlen);
}

static void mkdirs(const char *path)
{
	char t[PATH_MAX];
	strlcpy(t, path, sizeof t);
	for (char *p = t + 1; *p; p++)
		if (*p == '/') { *p = 0; mkdir(t, 0755); *p = '/'; }
	mkdir(t, 0755);
}

// Datei mit Inhalt anlegen (über eine Zwischendatei, damit Leser nie Halbes sehen)
static void put(const char *rel, const void *data, size_t n)
{
	char path[PATH_MAX], tmp[PATH_MAX];
	snprintf(path, sizeof path, "%s/%s", root, rel);
	char dir[PATH_MAX];
	strlcpy(dir, path, sizeof dir);
	char *sl = strrchr(dir, '/');
	if (sl) { *sl = 0; mkdirs(dir); }
	snprintf(tmp, sizeof tmp, "%s.%d", path, (int)getpid());
	int fd = open(tmp, O_WRONLY | O_CREAT | O_TRUNC, 0444);
	if (fd < 0) return;
	size_t o = 0;
	while (o < n) { ssize_t w = write(fd, (const char *)data + o, n - o); if (w <= 0) break; o += w; }
	close(fd);
	rename(tmp, path);
}
static void puts_(const char *rel, const char *s) { put(rel, s, strlen(s)); }

static void put_link(const char *rel, const char *target)
{
	char path[PATH_MAX];
	snprintf(path, sizeof path, "%s/%s", root, rel);
	char dir[PATH_MAX];
	strlcpy(dir, path, sizeof dir);
	char *sl = strrchr(dir, '/');
	if (sl) { *sl = 0; mkdirs(dir); }
	unlink(path);
	symlink(target, path);
}

static uint64_t sysctl_u64(const char *name)
{
	uint64_t v = 0;
	size_t l = sizeof v;
	if (sysctlbyname(name, &v, &l, NULL, 0)) return 0;
	if (l == 4) return *(uint32_t *)&v;
	return v;
}

static const char *comm_name(void)
{
	const char *b = strrchr(tux_exe, '/');
	return b ? b + 1 : tux_exe;
}

// --- Inhalte ---------------------------------------------------------------------------------
static void gen_cpuinfo(void)
{
	int n = (int)sysctl_u64("hw.ncpu");
	if (n < 1) n = 1;
	char *buf = malloc(n * 1024 + 16), *o = buf;
	char brand[128] = "unknown";
	size_t bl = sizeof brand;
	sysctlbyname("machdep.cpu.brand_string", brand, &bl, NULL, 0);
	for (int i = 0; i < n; i++) {
#ifdef TUX_X86_64
		o += sprintf(o, "processor\t: %d\nvendor_id\t: GenuineIntel\ncpu family\t: 6\nmodel\t\t: 58\nmodel name\t: %s\n"
		    "cpu MHz\t\t: 2000.000\ncache size\t: 4096 KB\ncpu cores\t: %d\nflags\t\t: fpu sse sse2 ssse3 sse4_1 sse4_2 popcnt\n"
		    "address sizes\t: 40 bits physical, 48 bits virtual\n\n", i, brand, n);
#else
		o += sprintf(o, "processor\t: %d\nBogoMIPS\t: 48.00\nFeatures\t: fp asimd evtstrm aes pmull sha1 sha2 crc32 atomics cpuid\n"
		    "CPU implementer\t: 0x61\nCPU architecture: 8\nCPU variant\t: 0x0\nCPU part\t: 0x000\nCPU revision\t: 0\n\n", i);
		(void)brand;
#endif
	}
	put("cpuinfo", buf, o - buf);
	free(buf);
}

static void gen_meminfo(void)
{
	uint64_t total = sysctl_u64("hw.memsize") / 1024, pg = tux_page / 1024;
	uint64_t freep = sysctl_u64("vm.page_free_count") * pg;
	if (freep > total) freep = total;
	char b[1024];
	int n = snprintf(b, sizeof b,
	    "MemTotal:       %8llu kB\nMemFree:        %8llu kB\nMemAvailable:   %8llu kB\nBuffers:               0 kB\nCached:                0 kB\n"
	    "SwapCached:            0 kB\nSwapTotal:             0 kB\nSwapFree:              0 kB\nShmem:                 0 kB\n"
	    "CommitLimit:    %8llu kB\nHugepagesize:       2048 kB\n",
	    (unsigned long long)total, (unsigned long long)freep, (unsigned long long)freep, (unsigned long long)total);
	put("meminfo", b, n);
}

static void gen_uptime(void)
{
	struct timeval bt;
	size_t l = sizeof bt;
	int mib[2] = {CTL_KERN, KERN_BOOTTIME};
	double up = 0;
	if (sysctl(mib, 2, &bt, &l, NULL, 0) == 0) {
		struct timeval now;
		gettimeofday(&now, NULL);
		up = (now.tv_sec - bt.tv_sec) + (now.tv_usec - bt.tv_usec) / 1e6;
	}
	char b[64];
	int n = snprintf(b, sizeof b, "%.2f %.2f\n", up, up);
	put("uptime", b, n);
}

static void gen_loadavg(void)
{
	double l[3] = {0, 0, 0};
	getloadavg(l, 3);
	char b[96];
	int n = snprintf(b, sizeof b, "%.2f %.2f %.2f 1/100 %d\n", l[0], l[1], l[2], (int)getpid());
	put("loadavg", b, n);
}

static void gen_maps(void)
{
	char *buf = malloc(1 << 20), *o = buf, *end = buf + (1 << 20) - 256;
	mach_vm_address_t addr = 0;
	for (;;) {
		mach_vm_size_t size = 0;
		vm_region_basic_info_data_64_t info;
		mach_msg_type_number_t cnt = VM_REGION_BASIC_INFO_COUNT_64;
		mach_port_t obj;
		if (mach_vm_region(mach_task_self(), &addr, &size, VM_REGION_BASIC_INFO_64, (vm_region_info_t)&info, &cnt, &obj) != KERN_SUCCESS)
			break;
		if (o < end)
			o += sprintf(o, "%08llx-%08llx %c%c%c%c %08x 00:00 0\n", (unsigned long long)addr, (unsigned long long)(addr + size),
			    (info.protection & VM_PROT_READ) ? 'r' : '-', (info.protection & VM_PROT_WRITE) ? 'w' : '-',
			    (info.protection & VM_PROT_EXECUTE) ? 'x' : '-', info.shared ? 's' : 'p', 0);
		addr += size;
	}
	put(".p/maps", buf, o - buf);
	free(buf);
}

static void task_sizes(uint64_t *virt, uint64_t *res)
{
	mach_task_basic_info_data_t ti;
	mach_msg_type_number_t c = MACH_TASK_BASIC_INFO_COUNT;
	*virt = *res = 0;
	if (task_info(mach_task_self(), MACH_TASK_BASIC_INFO, (task_info_t)&ti, &c) == KERN_SUCCESS) {
		*virt = ti.virtual_size;
		*res = ti.resident_size;
	}
}

static int nthreads(void)
{
	thread_array_t th;
	mach_msg_type_number_t n = 0;
	if (task_threads(mach_task_self(), &th, &n) != KERN_SUCCESS) return 1;
	for (unsigned i = 0; i < n; i++) mach_port_deallocate(mach_task_self(), th[i]);
	vm_deallocate(mach_task_self(), (vm_address_t)th, n * sizeof *th);
	return (int)n;
}

static void gen_status(void)
{
	uint64_t v, r;
	task_sizes(&v, &r);
	char b[1536];
	int n = snprintf(b, sizeof b,
	    "Name:\t%s\nUmask:\t%04o\nState:\tR (running)\nTgid:\t%d\nPid:\t%d\nPPid:\t%d\nTracerPid:\t0\n"
	    "Uid:\t%d\t%d\t%d\t%d\nGid:\t%d\t%d\t%d\t%d\nFDSize:\t256\nGroups:\t%d\n"
	    "VmPeak:\t%8llu kB\nVmSize:\t%8llu kB\nVmRSS:\t%8llu kB\nThreads:\t%d\nSigQ:\t0/0\n"
	    "SigPnd:\t0000000000000000\nSigBlk:\t0000000000000000\nSigIgn:\t0000000000000000\nSigCgt:\t0000000000000000\n"
	    "CapEff:\t0000000000000000\nCpus_allowed_list:\t0\n",
	    comm_name(), 022, (int)getpid(), (int)getpid(), (int)getppid(), (int)getuid(), (int)geteuid(), (int)getuid(), (int)getuid(),
	    (int)getgid(), (int)getegid(), (int)getgid(), (int)getgid(), (int)getgid(), (unsigned long long)(v / 1024),
	    (unsigned long long)(v / 1024), (unsigned long long)(r / 1024), nthreads());
	put(".p/status", b, n);
}

static void gen_stat(void)
{
	uint64_t v, r;
	task_sizes(&v, &r);
	char b[1024];
	int n = snprintf(b, sizeof b,
	    "%d (%s) R %d %d %d 0 -1 0 0 0 0 0 0 0 0 0 20 0 %d 0 0 %llu %llu 18446744073709551615 0 0 0 0 0 0 0 0 0 0 0 0 17 0 0 0 0 0 0\n",
	    (int)getpid(), comm_name(), (int)getppid(), (int)getpgrp(), (int)getsid(0), nthreads(), (unsigned long long)v,
	    (unsigned long long)(r / tux_page));
	put(".p/stat", b, n);
}

static void gen_statm(void)
{
	uint64_t v, r;
	task_sizes(&v, &r);
	char b[128];
	int n = snprintf(b, sizeof b, "%llu %llu 0 0 0 0 0\n", (unsigned long long)(v / tux_page), (unsigned long long)(r / tux_page));
	put(".p/statm", b, n);
}

static void gen_limits(void)
{
	static const struct { const char *name; int res; } t[] = {{"Max cpu time", RLIMIT_CPU}, {"Max file size", RLIMIT_FSIZE},
	    {"Max data size", RLIMIT_DATA}, {"Max stack size", RLIMIT_STACK}, {"Max core file size", RLIMIT_CORE},
	    {"Max resident set", RLIMIT_RSS}, {"Max processes", RLIMIT_NPROC}, {"Max open files", RLIMIT_NOFILE}};
	char b[2048], *o = b;
	o += sprintf(o, "Limit                     Soft Limit           Hard Limit           Units\n");
	for (unsigned i = 0; i < sizeof t / sizeof *t; i++) {
		struct rlimit rl;
		getrlimit(t[i].res, &rl);
		char s[24], h[24];
		if (rl.rlim_cur == RLIM_INFINITY) strcpy(s, "unlimited"); else snprintf(s, sizeof s, "%llu", (unsigned long long)rl.rlim_cur);
		if (rl.rlim_max == RLIM_INFINITY) strcpy(h, "unlimited"); else snprintf(h, sizeof h, "%llu", (unsigned long long)rl.rlim_max);
		o += sprintf(o, "%-25s %-20s %-20s\n", t[i].name, s, h);
	}
	put(".p/limits", b, o - b);
}

static void gen_fd(void)
{
	char d[PATH_MAX];
	snprintf(d, sizeof d, "%s/.p/fd", root);
	mkdirs(d);
	for (int i = 0; i < 256; i++) {
		char hp[PATH_MAX], rel[64];
		snprintf(rel, sizeof rel, ".p/fd/%d", i);
		if (fcntl(i, F_GETFD) < 0) {
			char path[PATH_MAX];
			snprintf(path, sizeof path, "%s/%s", root, rel);
			unlink(path);
			continue;
		}
		struct stat st;
		if (fcntl(i, F_GETPATH, hp) == 0) put_link(rel, hp);
		else if (fstat(i, &st) == 0 && S_ISSOCK(st.st_mode)) put_link(rel, "socket:[0]");
		else if (fstat(i, &st) == 0 && S_ISFIFO(st.st_mode)) put_link(rel, "pipe:[0]");
		else put_link(rel, "anon_inode:[0]");
	}
}

static void gen_proc_self(const char *name)
{
	put_link("self", ".p");
	char pid[16];
	snprintf(pid, sizeof pid, "%d", (int)getpid());
	put_link(pid, ".p");
	if (!strcmp(name, "maps")) gen_maps();
	else if (!strcmp(name, "status")) gen_status();
	else if (!strcmp(name, "stat")) gen_stat();
	else if (!strcmp(name, "statm")) gen_statm();
	else if (!strcmp(name, "limits")) gen_limits();
	else if (!strcmp(name, "fd")) gen_fd();
	else if (!strcmp(name, "cmdline")) put(".p/cmdline", g_cmdline ? g_cmdline : "", g_cmdlen);
	else if (!strcmp(name, "environ")) put(".p/environ", g_environ ? g_environ : "", g_envlen);
	else if (!strcmp(name, "comm")) { char b[64]; int n = snprintf(b, sizeof b, "%s\n", comm_name()); put(".p/comm", b, n); }
	else if (!strcmp(name, "mounts")) puts_(".p/mounts", "/dev/root / ext4 rw 0 0\n");
	else if (!strcmp(name, "mountinfo")) puts_(".p/mountinfo", "1 0 8:1 / / rw - ext4 /dev/root rw\n");
	else if (!strcmp(name, "exe") || !strcmp(name, "cwd") || !strcmp(name, "root")) {
		char host[PATH_MAX];
		const char *g = !strcmp(name, "exe") ? tux_exe : !strcmp(name, "cwd") ? tux_cwd : "/";
		{   // nicht tux_resolve: wir stecken selbst mitten in einer Auflösung
			tux_guest_to_host(g, host);
			char rel[32];
			snprintf(rel, sizeof rel, ".p/%s", name);
			size_t hl = strlen(host);
			if (hl > 1 && host[hl - 1] == '/') host[hl - 1] = 0;
			put_link(rel, host);
		}
	}
	else if (!strcmp(name, "task")) {
		char rel[64];
		snprintf(rel, sizeof rel, ".p/task/%d", (int)getpid());
		put_link(rel, "..");
	}
}

static void gen_top(const char *name)
{
	if (!strcmp(name, "cpuinfo")) gen_cpuinfo();
	else if (!strcmp(name, "meminfo")) gen_meminfo();
	else if (!strcmp(name, "uptime")) gen_uptime();
	else if (!strcmp(name, "loadavg")) gen_loadavg();
	else if (!strcmp(name, "version")) {
		char b[160];
		int n = snprintf(b, sizeof b, "Linux version 6.1.0-tux (tux@clokeos) (%s) #1 ClokeOS tux\n", TUX_NAME);
		put("version", b, n);
	} else if (!strcmp(name, "filesystems")) puts_("filesystems", "nodev\ttmpfs\nnodev\tproc\n\text4\n");
	else if (!strcmp(name, "mounts")) puts_("mounts", "/dev/root / ext4 rw 0 0\n");
	else if (!strcmp(name, "stat")) puts_("stat", "cpu  0 0 0 0 0 0 0 0 0 0\nintr 0\nctxt 0\nbtime 0\nprocesses 1\nprocs_running 1\nprocs_blocked 0\n");
	else if (!strcmp(name, "cmdline")) puts_("cmdline", "tux\n");
	else if (!strcmp(name, "sys")) {
		char b[160];
		int n = snprintf(b, sizeof b, "6.1.0-tux\n");
		put("sys/kernel/osrelease", b, n);
		puts_("sys/kernel/ostype", "Linux\n");
		puts_("sys/kernel/pid_max", "99999\n");
		puts_("sys/kernel/random/boot_id", "00000000-0000-4000-8000-000000000001\n");
		puts_("sys/vm/overcommit_memory", "1\n");
		puts_("sys/vm/max_map_count", "65530\n");
		puts_("sys/net/core/somaxconn", "128\n");
	}
}

// Bäume toter tux-Prozesse (ihr Name trägt die pid) wegräumen, einmal je Prozess
static void rm_tree(const char *path)
{
	DIR *d = opendir(path);
	if (d) {
		struct dirent *e;
		while ((e = readdir(d))) {
			if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
			char c[PATH_MAX];
			snprintf(c, sizeof c, "%s/%s", path, e->d_name);
			struct stat st;
			if (lstat(c, &st) == 0 && S_ISDIR(st.st_mode)) rm_tree(c); else unlink(c);
		}
		closedir(d);
		rmdir(path);
	} else unlink(path);
}
static void sweep(void)
{
	static int done;
	if (done) return;
	done = 1;
	DIR *d = opendir("/private/tmp");
	if (!d) return;
	struct dirent *e;
	while ((e = readdir(d))) {
		int pid;
		if (sscanf(e->d_name, ".tux-proc-%d", &pid) != 1 || pid == (int)getpid()) continue;
		if (kill(pid, 0) == 0 || errno != ESRCH) continue;
		char c[PATH_MAX];
		snprintf(c, sizeof c, "/private/tmp/%s", e->d_name);
		rm_tree(c);
	}
	closedir(d);
}


// --- andere Prozesse: /proc/<pid>/{stat,status,cmdline,comm,exe} aus libproc und KERN_PROCARGS2 ---------------------------
static void gen_pid(int pid)
{
	struct proc_bsdinfo bi;
	if (proc_pidinfo(pid, PROC_PIDTBSDINFO, 0, &bi, sizeof bi) != sizeof bi) return;
	struct proc_taskinfo ti;
	memset(&ti, 0, sizeof ti);
	proc_pidinfo(pid, PROC_PIDTASKINFO, 0, &ti, sizeof ti);
	char st = bi.pbi_status == 2 ? 'R' : bi.pbi_status == 4 ? 'T' : bi.pbi_status == 5 ? 'Z' : 'S';
	char rel[64], b[1536];
	int n;
	snprintf(rel, sizeof rel, "%d/status", pid);
	n = snprintf(b, sizeof b,
	    "Name:\t%s\nState:\t%c\nTgid:\t%d\nPid:\t%d\nPPid:\t%u\nTracerPid:\t0\nUid:\t%u\t%u\t%u\t%u\nGid:\t%u\t%u\t%u\t%u\n"
	    "VmSize:\t%8llu kB\nVmRSS:\t%8llu kB\nThreads:\t%d\n",
	    bi.pbi_comm, st, pid, pid, bi.pbi_ppid, bi.pbi_ruid, bi.pbi_uid, bi.pbi_svuid, bi.pbi_ruid, bi.pbi_rgid, bi.pbi_gid, bi.pbi_svgid,
	    bi.pbi_rgid, (unsigned long long)(ti.pti_virtual_size / 1024), (unsigned long long)(ti.pti_resident_size / 1024),
	    ti.pti_threadnum ? ti.pti_threadnum : 1);
	put(rel, b, n);
	snprintf(rel, sizeof rel, "%d/stat", pid);
	n = snprintf(b, sizeof b, "%d (%s) %c %u %u %u 0 -1 0 0 0 0 0 0 0 0 0 20 0 %d 0 %llu %llu %llu 18446744073709551615 0 0 0 0 0 0 0 0 0 0 0 0 17 0 0 0 0 0 0\n",
	    pid, bi.pbi_comm, st, bi.pbi_ppid, bi.pbi_pgid, 0u, ti.pti_threadnum ? ti.pti_threadnum : 1, (unsigned long long)bi.pbi_start_tvsec,
	    (unsigned long long)ti.pti_virtual_size, (unsigned long long)(ti.pti_resident_size / tux_page));
	put(rel, b, n);
	snprintf(rel, sizeof rel, "%d/comm", pid);
	n = snprintf(b, sizeof b, "%s\n", bi.pbi_comm);
	put(rel, b, n);
	// Befehlszeile: KERN_PROCARGS2 = argc, Programmpfad, NUL-Füllung, argv...
	int mib[3] = {CTL_KERN, KERN_PROCARGS2, pid};
	size_t sz = 0;
	snprintf(rel, sizeof rel, "%d/cmdline", pid);
	if (sysctl(mib, 3, NULL, &sz, NULL, 0) == 0 && sz > 8) {
		char *buf = malloc(sz);
		if (buf && sysctl(mib, 3, buf, &sz, NULL, 0) == 0) {
			int argc;
			memcpy(&argc, buf, 4);
			char *q = buf + 4, *e = buf + sz;
			while (q < e && *q) q++;
			while (q < e && !*q) q++;
			char *start = q;
			for (int i = 0; i < argc && q < e; i++) { while (q < e && *q) q++; q++; }
			put(rel, start, q > e ? e - start : q - start);
		} else put(rel, "", 0);
		free(buf);
	} else put(rel, "", 0);
	char path[PATH_MAX];
	if (proc_pidpath(pid, path, sizeof path) > 0) { snprintf(rel, sizeof rel, "%d/exe", pid); put_link(rel, path); }
}

static void gen_all_pids(void)
{
	int pids[2048];
	int n = proc_listallpids(pids, sizeof pids) ;
	if (n <= 0) return;
	if (n > 2048) n = 2048;
	for (int i = 0; i < n; i++) if (pids[i] > 0 && pids[i] != (int)getpid()) gen_pid(pids[i]);
}

// rest: der Teil des Pfads hinter /proc (leer oder mit führendem /). Liefert die Wurzel des erzeugten Baums.
int tux_proc_prepare(const char *rest, char *rootout)
{
	snprintf(root, sizeof root, "/private/tmp/.tux-proc-%d", (int)getpid());
	sweep();
	mkdirs(root);
	while (*rest == '/') rest++;
	char first[64] = "", second[64] = "";
	const char *p = rest;
	size_t n = strcspn(p, "/");
	snprintf(first, sizeof first, "%.*s", (int)(n < 63 ? n : 63), p);
	p += n;
	while (*p == '/') p++;
	n = strcspn(p, "/");
	snprintf(second, sizeof second, "%.*s", (int)(n < 63 ? n : 63), p);
	if (!*first) {   // /proc selbst aufzählen
		const char *top[] = {"cpuinfo", "meminfo", "uptime", "loadavg", "version", "filesystems", "mounts", "stat", "cmdline", "sys"};
		for (unsigned i = 0; i < sizeof top / sizeof *top; i++) gen_top(top[i]);
		gen_proc_self("status");
		gen_all_pids();
	} else if (!strcmp(first, "self") || !strcmp(first, "thread-self") || atoi(first) == (int)getpid()) {
		if (!*second) {
			const char *all[] = {"maps", "status", "stat", "statm", "cmdline", "environ", "comm", "mounts", "mountinfo", "limits", "fd", "exe", "cwd", "root", "task"};
			for (unsigned i = 0; i < sizeof all / sizeof *all; i++) gen_proc_self(all[i]);
		} else gen_proc_self(second);
		if (!strcmp(first, "thread-self")) put_link("thread-self", ".p");
	} else if (atoi(first) > 0) gen_pid(atoi(first));
	else gen_top(first);
	strlcpy(rootout, root, PATH_MAX);
	return 0;
}
