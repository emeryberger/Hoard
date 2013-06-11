#include <pthread.h>

#if defined(__APPLE__) || !defined(_POSIX_BARRIERS)

#undef pthread_barrier_t

typedef struct {
  int needed;
  int called;
  pthread_mutex_t mutex;
  pthread_cond_t cond;
} pthread_barrier_t;

#if defined(__cplusplus)
extern "C" {
#endif

  int pthread_barrier_init(pthread_barrier_t *barrier, void * attr, int needed);
  int pthread_barrier_destroy(pthread_barrier_t *barrier);
  int pthread_barrier_wait(pthread_barrier_t *barrier);
  
  int pthread_barrier_init(pthread_barrier_t *barrier, void * attr, int needed)
  {
    barrier->needed = needed;
    barrier->called = 0;
    pthread_mutex_init(&barrier->mutex,NULL);
    pthread_cond_init(&barrier->cond,NULL);
    return 0;
  }
  
  int pthread_barrier_destroy(pthread_barrier_t *barrier)
  {
    pthread_mutex_destroy(&barrier->mutex);
    pthread_cond_destroy(&barrier->cond);
    return 0;
  }
  
  int pthread_barrier_wait(pthread_barrier_t *barrier)
  {
    pthread_mutex_lock(&barrier->mutex);
    barrier->called++;
    if (barrier->called == barrier->needed) {
      barrier->called = 0;
      pthread_cond_broadcast(&barrier->cond);
    } else {
      pthread_cond_wait(&barrier->cond,&barrier->mutex);
    }
    pthread_mutex_unlock(&barrier->mutex);
    return 0;
  }

#if defined(__cplusplus)
}
#endif

#endif
