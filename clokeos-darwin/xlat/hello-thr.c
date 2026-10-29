// Dynamisches x86_64-Testprogramm mit Threads: 4 Threads zählen unter einem Mutex, pthread_join.
#include <pthread.h>
#include <stdio.h>
static pthread_mutex_t m = PTHREAD_MUTEX_INITIALIZER;
static long counter;
static void *work(void *arg) {
	for (int i = 0; i < 20000; i++) { pthread_mutex_lock(&m); counter++; pthread_mutex_unlock(&m); }
	return arg;
}
int main(void) {
	pthread_t t[4];
	for (long i = 0; i < 4; i++) if (pthread_create(&t[i], 0, work, (void *)(i + 1))) { puts("pthread_create fehlgeschlagen"); return 1; }
	long sum = 0;
	for (int i = 0; i < 4; i++) { void *r; pthread_join(t[i], &r); sum += (long)r; }
	printf("counter=%ld (erwartet 80000) rueckgaben=%ld (erwartet 10)\n", counter, sum);
	return counter == 80000 && sum == 10 ? 0 : 2;
}
