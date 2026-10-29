// Dynamisches x86_64-Testprogramm: umkämpfte Mutexe. Der Besitzer schläft im kritischen Abschnitt, damit die anderen Threads wirklich blockieren.
#include <pthread.h>
#include <stdio.h>
#include <unistd.h>
static pthread_mutex_t m = PTHREAD_MUTEX_INITIALIZER, rm;
static long counter, rcount;
static int inside, overlap;
static void *work(void *a) {
	for (int i = 0; i < 300; i++) {
		pthread_mutex_lock(&m);
		if (++inside != 1) overlap++;
		long c = counter; if (i % 2 == 0) usleep(200); counter = c + 1;  // Wettlauf, falls der Mutex nicht hält
		inside--;
		pthread_mutex_unlock(&m);
	}
	return a;
}
static void *rwork(void *a) {
	for (int i = 0; i < 100; i++) { pthread_mutex_lock(&rm); pthread_mutex_lock(&rm); rcount++; if (i % 5 == 0) usleep(100); pthread_mutex_unlock(&rm); pthread_mutex_unlock(&rm); }
	return a;
}
int main(void) {
	pthread_t t[6]; int fails = 0;
	for (int i = 0; i < 6; i++) pthread_create(&t[i], 0, work, 0);
	for (int i = 0; i < 6; i++) pthread_join(t[i], 0);
	if (counter != 1800 || overlap) { printf("FEHLER: counter=%ld overlap=%d\n", counter, overlap); fails++; }
	pthread_mutexattr_t at; pthread_mutexattr_init(&at); pthread_mutexattr_settype(&at, PTHREAD_MUTEX_RECURSIVE); pthread_mutex_init(&rm, &at);
	for (int i = 0; i < 4; i++) pthread_create(&t[i], 0, rwork, 0);
	for (int i = 0; i < 4; i++) pthread_join(t[i], 0);
	if (rcount != 400) { printf("FEHLER: rcount=%ld\n", rcount); fails++; }
	pthread_mutex_lock(&m); int tl = pthread_mutex_trylock(&m); pthread_mutex_unlock(&m);
	if (tl == 0) { printf("FEHLER: trylock auf gehaltenem Mutex\n"); fails++; }
	printf("hello-mtx: %s (counter=%ld rcount=%ld)\n", fails ? "FEHLER" : "ok", counter, rcount);
	return fails;
}
