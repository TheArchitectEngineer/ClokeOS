// Dynamisches x86_64-Testprogramm: Bedingungsvariablen (wait/signal/broadcast/timedwait), Erzeuger/Verbraucher, Mach-Semaphore.
#include <errno.h>
#include <mach/mach.h>
#include <mach/semaphore.h>
#include <pthread.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>
static pthread_mutex_t m = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t cv = PTHREAD_COND_INITIALIZER, cv_go = PTHREAD_COND_INITIALIZER;
static int queue[16], qn, produced, consumed_sum, go, waiting;
static semaphore_t sem;
static int fails;
#define CHECK(c, msg) do { if (!(c)) { printf("FEHLER: %s\n", msg); fails++; } } while (0)
static void *producer(void *a) {
	for (int i = 1; i <= 100; i++) {
		pthread_mutex_lock(&m);
		while (qn == 16) pthread_cond_wait(&cv, &m);
		queue[qn++] = i; produced++; pthread_cond_broadcast(&cv);
		pthread_mutex_unlock(&m);
	}
	return 0;
}
static void *consumer(void *a) {
	for (int got = 0; got < 100; got++) {
		pthread_mutex_lock(&m);
		while (qn == 0) pthread_cond_wait(&cv, &m);
		consumed_sum += queue[--qn]; pthread_cond_broadcast(&cv);
		pthread_mutex_unlock(&m);
	}
	return 0;
}
static void *waiter(void *a) {
	pthread_mutex_lock(&m); waiting++;
	while (!go) pthread_cond_wait(&cv_go, &m);
	pthread_mutex_unlock(&m); return (void *)1;
}
static void *semwaiter(void *a) { kern_return_t k = semaphore_wait(sem); return (void *)(long)(k == KERN_SUCCESS); }
int main(void) {
	pthread_t p, c, w[3], sw; void *r;
	pthread_create(&p, 0, producer, 0); pthread_create(&c, 0, consumer, 0);
	pthread_join(p, 0); pthread_join(c, 0);
	CHECK(consumed_sum == 5050, "Erzeuger/Verbraucher");
	for (int i = 0; i < 3; i++) pthread_create(&w[i], 0, waiter, 0);
	for (;;) { pthread_mutex_lock(&m); int n = waiting; pthread_mutex_unlock(&m); if (n == 3) break; usleep(1000); }
	pthread_mutex_lock(&m); go = 1; pthread_cond_broadcast(&cv_go); pthread_mutex_unlock(&m);
	long s = 0; for (int i = 0; i < 3; i++) { pthread_join(w[i], &r); s += (long)r; } CHECK(s == 3, "broadcast weckt alle");
	struct timespec ts; clock_gettime(CLOCK_REALTIME, &ts); ts.tv_nsec += 100000000; if (ts.tv_nsec >= 1000000000) { ts.tv_sec++; ts.tv_nsec -= 1000000000; }
	pthread_mutex_lock(&m); int e = pthread_cond_timedwait(&cv_go, &m, &ts); pthread_mutex_unlock(&m); CHECK(e == ETIMEDOUT, "timedwait -> ETIMEDOUT");
	kern_return_t k = semaphore_create(mach_task_self(), &sem, SYNC_POLICY_FIFO, 0); CHECK(k == KERN_SUCCESS, "semaphore_create");
	pthread_create(&sw, 0, semwaiter, 0); usleep(20000); semaphore_signal(sem); pthread_join(sw, &r); CHECK((long)r == 1, "semaphore_wait/signal");
	mach_timespec_t mt = {0, 50000000}; k = semaphore_timedwait(sem, mt); CHECK(k == KERN_OPERATION_TIMED_OUT, "semaphore_timedwait Zeitüberschreitung");
	printf("hello-cv: %s (sum=%d)\n", fails ? "FEHLER" : "ok", consumed_sum);
	return fails;
}
