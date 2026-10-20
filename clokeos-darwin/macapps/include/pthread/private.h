/* Ersatz für <pthread/private.h> */
#ifndef CLOKE_PTHREAD_PRIVATE_H
#define CLOKE_PTHREAD_PRIVATE_H
#include <pthread.h>
__BEGIN_DECLS
pthread_t pthread_main_thread_np(void);
int pthread_key_init_np(int key, void (*destructor)(void *));
__END_DECLS
#endif
