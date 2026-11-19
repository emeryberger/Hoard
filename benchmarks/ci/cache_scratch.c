/*
 * cache_scratch.c - Test for passive-false sharing of cache lines
 *
 * Based on Hoard's cache-scratch benchmark. Tests whether an allocator
 * causes cache-line contention by allocating objects from different
 * threads close together in memory.
 *
 * Usage: cache_scratch [threads] [iterations] [object_size] [repetitions]
 */

#include "bench_common.h"

#define DEFAULT_THREADS 4
#define DEFAULT_ITERATIONS 1000000
#define DEFAULT_OBJ_SIZE 8
#define DEFAULT_REPETITIONS 1

static int obj_size;
static int iterations;
static int repetitions;

typedef struct {
  char* obj;
} thread_arg_t;

static BENCH_THREAD_RETURN worker(void* arg) {
  thread_arg_t* ta = (thread_arg_t*)arg;
  char* obj = ta->obj;
  int sz = obj_size;

  for (int i = 0; i < iterations; i++) {
    free(obj);
    obj = (char*)malloc(sz);
    if (obj) {
      /* Write the object repeatedly. Through a volatile pointer: plain
         stores to an object that is freed right afterwards are dead, and
         the compiler deletes them -- leaving a bare free/malloc loop that
         never touches memory and so cannot detect false sharing. */
      volatile char* v = obj;
      for (int r = 0; r < repetitions; r++) {
        for (int j = 0; j < sz; j++) v[j] = (char)j;
        for (int j = 0; j < sz; j++) v[j] = (char)(v[j] + 1);
      }
    }
  }

  free(obj);
  return BENCH_THREAD_RETURN_VALUE;
}

int main(int argc, char** argv) {
  int nthreads = (argc > 1) ? atoi(argv[1]) : DEFAULT_THREADS;
  iterations = (argc > 2) ? atoi(argv[2]) : DEFAULT_ITERATIONS;
  obj_size = (argc > 3) ? atoi(argv[3]) : DEFAULT_OBJ_SIZE;
  repetitions = (argc > 4) ? atoi(argv[4]) : DEFAULT_REPETITIONS;

  bench_timer_t start;
  bench_thread_t* threads = (bench_thread_t*)malloc(sizeof(bench_thread_t) * nthreads);
  thread_arg_t* args = (thread_arg_t*)malloc(sizeof(thread_arg_t) * nthreads);

  /* Pre-allocate objects for each thread */
  for (int i = 0; i < nthreads; i++) {
    args[i].obj = (char*)malloc(obj_size);
  }

  bench_timer_init();
  bench_timer_start(&start);

  for (int i = 0; i < nthreads; i++) {
    bench_thread_create(&threads[i], worker, &args[i]);
  }

  bench_thread_join_all(threads, nthreads);

  double elapsed = bench_timer_elapsed(&start);
  long total_ops = (long)nthreads * iterations;
  bench_report("cache-scratch: threads=%d iterations=%d obj_size=%d time=%.3f sec (%.0f ops/sec)\n",
         nthreads, iterations, obj_size, elapsed, total_ops / elapsed);

  free(threads);
  free(args);

  return 0;
}
