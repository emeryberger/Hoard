/*
 * cache_scratch.c - Test for passive-false sharing of cache lines
 *
 * Based on Hoard's cache-scratch benchmark. Tests whether an allocator
 * causes cache-line contention by allocating objects from different
 * threads close together in memory.
 *
 * Usage: cache_scratch [threads] [iterations] [object_size]
 */

#include "bench_common.h"

#define DEFAULT_THREADS 4
#define DEFAULT_ITERATIONS 1000000
#define DEFAULT_OBJ_SIZE 8

static int obj_size;
static int iterations;

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
      /* Touch every byte to stress cache */
      for (int j = 0; j < sz; j++) obj[j] = (char)j;
      for (int j = 0; j < sz; j++) obj[j] = (char)(obj[j] + 1);
    }
  }

  free(obj);
  return BENCH_THREAD_RETURN_VALUE;
}

int main(int argc, char** argv) {
  int nthreads = (argc > 1) ? atoi(argv[1]) : DEFAULT_THREADS;
  iterations = (argc > 2) ? atoi(argv[2]) : DEFAULT_ITERATIONS;
  obj_size = (argc > 3) ? atoi(argv[3]) : DEFAULT_OBJ_SIZE;

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
