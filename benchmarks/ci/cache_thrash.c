/*
 * cache_thrash.c - Heap cache locality test
 *
 * Part of Hoard benchmarking suite. Tests heap cache locality by
 * allocating and accessing objects in patterns that stress the cache.
 *
 * Usage: cache_thrash [threads] [iterations] [object_size] [working_set]
 */

#include "bench_common.h"

#define DEFAULT_THREADS 4
#define DEFAULT_ITERATIONS 1000000
#define DEFAULT_OBJ_SIZE 64
#define DEFAULT_WORKING_SET 1000

static int obj_size;
static int iterations;
static int working_set;

typedef struct {
  int tid;
  unsigned int seed;
} thread_arg_t;

static BENCH_THREAD_RETURN worker(void* arg) {
  thread_arg_t* ta = (thread_arg_t*)arg;
  unsigned int seed = ta->seed;

  /* Allocate working set */
  char** ptrs = (char**)malloc(working_set * sizeof(char*));
  for (int i = 0; i < working_set; i++) {
    ptrs[i] = (char*)malloc(obj_size);
    if (ptrs[i]) memset(ptrs[i], (char)i, obj_size);
  }

  /* Thrash: randomly access and reallocate */
  for (int i = 0; i < iterations; i++) {
    int idx = bench_rand(&seed) % working_set;

    /* Access pattern that thrashes cache */
    if (ptrs[idx]) {
      for (int j = 0; j < obj_size; j += 64) {
        ptrs[idx][j] = (char)(ptrs[idx][j] + 1);
      }
    }

    /* Occasionally reallocate */
    if ((i % 10) == 0) {
      free(ptrs[idx]);
      ptrs[idx] = (char*)malloc(obj_size);
      if (ptrs[idx]) memset(ptrs[idx], (char)i, obj_size);
    }
  }

  /* Cleanup */
  for (int i = 0; i < working_set; i++) {
    if (ptrs[i]) free(ptrs[i]);
  }
  free(ptrs);

  return BENCH_THREAD_RETURN_VALUE;
}

int main(int argc, char** argv) {
  int nthreads = (argc > 1) ? atoi(argv[1]) : DEFAULT_THREADS;
  iterations = (argc > 2) ? atoi(argv[2]) : DEFAULT_ITERATIONS;
  obj_size = (argc > 3) ? atoi(argv[3]) : DEFAULT_OBJ_SIZE;
  working_set = (argc > 4) ? atoi(argv[4]) : DEFAULT_WORKING_SET;

  bench_timer_t start;
  bench_thread_t* threads = (bench_thread_t*)malloc(sizeof(bench_thread_t) * nthreads);
  thread_arg_t* args = (thread_arg_t*)malloc(sizeof(thread_arg_t) * nthreads);

  bench_timer_init();
  bench_timer_start(&start);

  for (int i = 0; i < nthreads; i++) {
    args[i].tid = i;
    args[i].seed = i + 1;
    bench_thread_create(&threads[i], worker, &args[i]);
  }

  bench_thread_join_all(threads, nthreads);

  double elapsed = bench_timer_elapsed(&start);
  long total_ops = (long)nthreads * iterations;
  bench_report("cache-thrash: threads=%d iterations=%d obj_size=%d working_set=%d time=%.3f sec (%.0f ops/sec)\n",
         nthreads, iterations, obj_size, working_set, elapsed, total_ops / elapsed);

  free(threads);
  free(args);

  return 0;
}
