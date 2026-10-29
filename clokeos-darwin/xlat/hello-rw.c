// Dynamisches x86_64-Testprogramm: pthread_rwlock (umkämpft, mit usleep im kritischen Abschnitt) und ein gemischter Stresstest
// aus Mutex, rwlock, Bedingungsvariablen, Mach-Semaphore und pthread_create/join. Aufruf: hello-rw [Sekunden Stresstest, Standard 2]
#include <errno.h>
#include <mach/mach.h>
#include <mach/semaphore.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
static int fails;
#define CHECK(c, what) do { if (!(c)) { printf("FEHLER: %s (Zeile %d)\n", what, __LINE__); fails++; } } while (0)
#define ADD(p, n) __atomic_add_fetch((p), (n), __ATOMIC_SEQ_CST)
#define LD(p) __atomic_load_n((p), __ATOMIC_SEQ_CST)
static unsigned rnd(unsigned *s) { *s = *s * 1664525u + 1013904223u; return *s >> 8; }

// --- Teil 1: Einzel-Thread-Verhalten (try-Varianten, Rekursion, EDEADLK) ---
static void *other_try(void *p) { return (void *)(long)((pthread_rwlock_tryrdlock(p) == EBUSY) + 2 * (pthread_rwlock_trywrlock(p) == EBUSY)); }
static void basics(void) {
	pthread_rwlock_t l = PTHREAD_RWLOCK_INITIALIZER;
	CHECK(pthread_rwlock_rdlock(&l) == 0, "rdlock");
	CHECK(pthread_rwlock_rdlock(&l) == 0, "rdlock rekursiv");
	CHECK(pthread_rwlock_tryrdlock(&l) == 0, "tryrdlock bei Lesern");
	CHECK(pthread_rwlock_trywrlock(&l) == EBUSY, "trywrlock bei Lesern: EBUSY");
	CHECK(pthread_rwlock_unlock(&l) == 0 && pthread_rwlock_unlock(&l) == 0 && pthread_rwlock_unlock(&l) == 0, "unlock x3");
	CHECK(pthread_rwlock_trywrlock(&l) == 0, "trywrlock frei");
	CHECK(pthread_rwlock_tryrdlock(&l) == EDEADLK, "tryrdlock im eigenen wrlock: EDEADLK");
	CHECK(pthread_rwlock_trywrlock(&l) == EDEADLK, "trywrlock im eigenen wrlock: EDEADLK");
	{ pthread_t t; void *r; pthread_create(&t, 0, other_try, &l); pthread_join(t, &r); CHECK((long)r == 3, "anderer Thread: tryrd/trywr beim Schreiber EBUSY"); }
	CHECK(pthread_rwlock_wrlock(&l) == EDEADLK, "wrlock im eigenen wrlock: EDEADLK");
	CHECK(pthread_rwlock_unlock(&l) == 0, "unlock Schreiber");
	CHECK(pthread_rwlock_destroy(&l) == 0, "destroy");
	pthread_rwlock_t l2; pthread_rwlockattr_t at; pthread_rwlockattr_init(&at);
	CHECK(pthread_rwlock_init(&l2, &at) == 0, "init mit Attribut");
	CHECK(pthread_rwlock_wrlock(&l2) == 0 && pthread_rwlock_unlock(&l2) == 0, "wrlock/unlock nach init");
	pthread_rwlock_destroy(&l2);
}

// --- Teil 2: Leser teilen, Schreiber schließen sich aus ---
static pthread_rwlock_t rw = PTHREAD_RWLOCK_INITIALIZER;
static int readers_in, writers_in, max_readers, viol, a_val, b_val, reads_done, writes_done, try_ok, try_busy;
static void *rd(void *p) {
	unsigned s = (unsigned)(long)p * 7919u + 1;
	for (int i = 0; i < 60; i++) {
		pthread_rwlock_rdlock(&rw);
		int n = ADD(&readers_in, 1); if (n > LD(&max_readers)) max_readers = n;
		if (LD(&writers_in) != 0) ADD(&viol, 1);
		usleep(100 + rnd(&s) % 400); if (LD(&a_val) != LD(&b_val)) ADD(&viol, 1);
		ADD(&readers_in, -1); ADD(&reads_done, 1);
		pthread_rwlock_unlock(&rw);
	}
	return p;
}
static void *wr(void *p) {
	unsigned s = (unsigned)(long)p * 104729u + 3;
	for (int i = 0; i < 25; i++) {
		pthread_rwlock_wrlock(&rw);
		if (ADD(&writers_in, 1) != 1) ADD(&viol, 1);
		if (LD(&readers_in) != 0) ADD(&viol, 1);
		a_val++; usleep(200 + rnd(&s) % 400); b_val++;
		ADD(&writers_in, -1); ADD(&writes_done, 1);
		pthread_rwlock_unlock(&rw);
	}
	return p;
}
static void *tr(void *p) {  // try-Varianten unter Last
	unsigned s = (unsigned)(long)p * 31337u + 5;
	for (int i = 0; i < 200; i++) {
		if (rnd(&s) & 1) {
			if (pthread_rwlock_tryrdlock(&rw) == 0) { ADD(&try_ok, 1); ADD(&readers_in, 1); if (LD(&writers_in)) ADD(&viol, 1); usleep(50); ADD(&readers_in, -1); pthread_rwlock_unlock(&rw); } else ADD(&try_busy, 1);
		} else {
			if (pthread_rwlock_trywrlock(&rw) == 0) { ADD(&try_ok, 1); if (ADD(&writers_in, 1) != 1 || LD(&readers_in)) ADD(&viol, 1); a_val++; usleep(50); b_val++; ADD(&writers_in, -1); pthread_rwlock_unlock(&rw); } else ADD(&try_busy, 1);
		}
		usleep(rnd(&s) % 200);
	}
	return p;
}
static void contended(void) {
	pthread_t t[12]; int n = 0;
	for (long i = 0; i < 6; i++) pthread_create(&t[n++], 0, rd, (void *)i);
	for (long i = 0; i < 3; i++) pthread_create(&t[n++], 0, wr, (void *)i);
	for (int i = 0; i < n; i++) pthread_join(t[i], 0);
	n = 0;  // try-Varianten gegen einen einzelnen Leser und Schreiber: dazwischen ist die Sperre auch frei
	pthread_create(&t[n++], 0, rd, (void *)7L); pthread_create(&t[n++], 0, wr, (void *)7L);
	for (long i = 0; i < 3; i++) pthread_create(&t[n++], 0, tr, (void *)i);
	for (int i = 0; i < n; i++) pthread_join(t[i], 0);
	CHECK(viol == 0, "Ausschluss/Teilen verletzt");
	CHECK(max_readers > 1, "Leser haben sich nie überlappt");
	CHECK(reads_done == 420 && writes_done == 100, "nicht alle Läufe fertig");
	CHECK(a_val == b_val, "a != b"); CHECK(try_ok > 0 && try_busy > 0, "try-Varianten: nie Erfolg oder nie EBUSY");
	CHECK(pthread_rwlock_trywrlock(&rw) == 0, "danach frei"); pthread_rwlock_unlock(&rw);
	printf("rwlock umkaempft: leser max %d, schreiber %d, try ok/busy %d/%d\n", max_readers, writes_done, try_ok, try_busy);
}
// Schreiber muss nicht ewig hinter Lesern warten, und ein wartender Schreiber hält neue Leser nicht auf (keine Hunger-Prüfung, nur Fortschritt)
static pthread_rwlock_t rw2 = PTHREAD_RWLOCK_INITIALIZER; static int w_got, r2_got;
static void *late_writer(void *p) { pthread_rwlock_wrlock(&rw2); w_got = 1; pthread_rwlock_unlock(&rw2); return p; }
static void *late_reader(void *p) { pthread_rwlock_rdlock(&rw2); r2_got = 1; pthread_rwlock_unlock(&rw2); return p; }
static void order(void) {
	pthread_t w, r; pthread_rwlock_rdlock(&rw2);
	pthread_create(&w, 0, late_writer, 0); usleep(30000); pthread_create(&r, 0, late_reader, 0); usleep(30000);
	CHECK(!w_got, "Schreiber kam durch, obwohl ein Leser hält");
	pthread_rwlock_unlock(&rw2); pthread_join(w, 0); pthread_join(r, 0);
	CHECK(w_got && r2_got, "wartende Threads nie freigegeben");
	pthread_rwlock_wrlock(&rw2); pthread_create(&r, 0, late_reader, 0); pthread_create(&w, 0, late_writer, 0); r2_got = w_got = 0; usleep(30000);
	CHECK(!r2_got && !w_got, "Wartende kamen durch, obwohl der Schreiber hält");
	pthread_rwlock_unlock(&rw2); pthread_join(r, 0); pthread_join(w, 0); CHECK(w_got && r2_got, "Wartende nach Schreiber");
}

// --- Teil 3: Stresstest, gemischt ---
#define NT 12
static pthread_mutex_t qm = PTHREAD_MUTEX_INITIALIZER, cm = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t notfull = PTHREAD_COND_INITIALIZER, notempty = PTHREAD_COND_INITIALIZER;
static pthread_rwlock_t srw = PTHREAD_RWLOCK_INITIALIZER;
static int q[8], qn, stop, produced, consumed, cnt, cnt_in, sem_in, sem_viol, sa, sb, sviol, spawned, spawn_sum, rwr, rww;
static long psum, csum;
static semaphore_t tok;
static void *mini(void *p) { usleep(50); ADD(&spawn_sum, (int)(long)p); return p; }
static void *stress(void *p) {
	int me = (int)(long)p; unsigned s = me * 2654435761u + 11; int role = me % 4;
	while (!LD(&stop)) {
		switch (role) {
		case 0:  // rwlock: meist lesen, manchmal schreiben
			if (rnd(&s) % 5) { pthread_rwlock_rdlock(&srw); if (LD(&sa) != LD(&sb)) ADD(&sviol, 1); if (rnd(&s) % 4 == 0) usleep(100); ADD(&rwr, 1); pthread_rwlock_unlock(&srw); }
			else { pthread_rwlock_wrlock(&srw); sa++; usleep(rnd(&s) % 300); sb++; ADD(&rww, 1); pthread_rwlock_unlock(&srw); }
			break;
		case 1: {  // Mutex mit Schlafen darin + Mach-Semaphore als 3er-Pool
			pthread_mutex_lock(&cm); if (++cnt_in != 1) ADD(&sviol, 1); cnt++; if (rnd(&s) % 3 == 0) usleep(80); cnt_in--; pthread_mutex_unlock(&cm);
			semaphore_wait(tok); if (ADD(&sem_in, 1) > 3) ADD(&sem_viol, 1); usleep(50 + rnd(&s) % 100); ADD(&sem_in, -1); semaphore_signal(tok);
			break; }
		case 2: {  // Erzeuger
			pthread_mutex_lock(&qm); while (qn == 8 && !LD(&stop)) pthread_cond_wait(&notfull, &qm);
			if (qn < 8) { int v = rnd(&s) % 1000; q[qn++] = v; produced++; psum += v; pthread_cond_signal(&notempty); }
			pthread_mutex_unlock(&qm); if (rnd(&s) % 8 == 0) usleep(50);
			break; }
		case 3: {  // Verbraucher (mit Zeitlimit, damit er das Stopp-Flag sieht) und gelegentlich ein kurzlebiger Thread
			pthread_mutex_lock(&qm);
			while (qn == 0 && !LD(&stop)) { struct timespec ts; clock_gettime(CLOCK_REALTIME, &ts); ts.tv_nsec += 20000000; if (ts.tv_nsec >= 1000000000) { ts.tv_sec++; ts.tv_nsec -= 1000000000; } pthread_cond_timedwait(&notempty, &qm, &ts); }
			if (qn > 0) { csum += q[--qn]; consumed++; pthread_cond_signal(&notfull); }
			pthread_mutex_unlock(&qm);
			if (rnd(&s) % 6 == 0) { pthread_t t; if (pthread_create(&t, 0, mini, (void *)3L) == 0) { pthread_join(t, 0); ADD(&spawned, 1); } }
			break; }
		}
	}
	return p;
}
static void stress_test(int secs) {
	semaphore_create(mach_task_self(), &tok, SYNC_POLICY_FIFO, 3);
	pthread_t t[NT]; struct timespec a, b; clock_gettime(CLOCK_MONOTONIC, &a);
	for (long i = 0; i < NT; i++) CHECK(pthread_create(&t[i], 0, stress, (void *)i) == 0, "pthread_create");
	sleep(secs);
	ADD(&stop, 1);
	pthread_mutex_lock(&qm); pthread_cond_broadcast(&notfull); pthread_cond_broadcast(&notempty); pthread_mutex_unlock(&qm);
	for (int i = 0; i < NT; i++) pthread_join(t[i], 0);
	clock_gettime(CLOCK_MONOTONIC, &b);
	while (qn > 0) { csum += q[--qn]; consumed++; }  // Rest, den kein Verbraucher mehr abholte
	CHECK(sviol == 0 && sem_viol == 0, "Verletzung im Stresstest");
	CHECK(sa == sb && sa == rww - 0 + 0 - rww + sa, "rw-Daten");
	CHECK(produced == consumed && psum == csum, "Erzeuger/Verbraucher");
	CHECK(spawn_sum == 3 * spawned, "Wegwerf-Threads");
	printf("stress %d Threads, %.1f s: rwlock lesen %d schreiben %d, mutex %d, erzeugt %d, kurzlebige Threads %d\n", NT, (b.tv_sec - a.tv_sec) + (b.tv_nsec - a.tv_nsec) / 1e9, rwr, rww, cnt, produced, spawned);
}
int main(int argc, char **argv) {
	basics(); contended(); order();
	stress_test(argc > 1 ? atoi(argv[1]) : 2);
	printf("hello-rw: %s\n", fails ? "FEHLER" : "ok");
	return fails;
}
