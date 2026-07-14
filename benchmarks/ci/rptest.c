/*
 * rptest.c - rpmalloc-style benchmark
 *
 * Based on rpmalloc-benchmark. Tests allocation patterns common in
 * game engines and real-time applications.
 *
 * Usage: rptest [threads] [loops] [allocs_per_loop]
 */

#include "bench_common.h"

#define DEFAULT_THREADS 4
#define DEFAULT_LOOPS 1000
#define DEFAULT_ALLOCS 10000
#define MAX_SIZE 16000
#define MIN_SIZE 8

static volatile long total_allocs = 0;
static int loops;
static int allocs_per_loop;

typedef struct {
  int tid;
  unsigned int seed;
} thread_arg_t;

static BENCH_THREAD_RETURN worker(void* arg) {
  thread_arg_t* ta = (thread_arg_t*)arg;
  unsigned int seed = ta->seed;

  for (int loop = 0; loop < loops; loop++) {
    void** ptrs = (void**)malloc(allocs_per_loop * sizeof(void*));

    /* Allocate phase */
    for (int i = 0; i < allocs_per_loop; i++) {
      size_t size = (bench_rand(&seed) % (MAX_SIZE - MIN_SIZE)) + MIN_SIZE;
      ptrs[i] = malloc(size);
      if (ptrs[i]) ((char*)ptrs[i])[0] = (char)i;
      bench_atomic_inc(&total_allocs);
    }

    /* Random access phase */
    for (int i = 0; i < allocs_per_loop / 10; i++) {
      int idx = bench_rand(&seed) % allocs_per_loop;
      if (ptrs[idx]) {
        size_t new_size = (bench_rand(&seed) % (MAX_SIZE - MIN_SIZE)) + MIN_SIZE;
        void* p = realloc(ptrs[idx], new_size);
        if (p) {
          ptrs[idx] = p;
          ((char*)p)[0] = (char)i;
        }
      }
    }

    /* Free phase - some in order, some random */
    for (int i = 0; i < allocs_per_loop; i++) {
      int idx;
      if ((i % 3) == 0) {
        /* Random free */
        idx = bench_rand(&seed) % allocs_per_loop;
      } else {
        /* Sequential free */
        idx = i;
      }
      if (ptrs[idx]) {
        free(ptrs[idx]);
        ptrs[idx] = NULL;
      }
    }

    /* Free any remaining */
    for (int i = 0; i < allocs_per_loop; i++) {
      if (ptrs[i]) free(ptrs[i]);
    }

    free(ptrs);
  }

  return BENCH_THREAD_RETURN_VALUE;
}

int main(int argc, char** argv) {
  int nthreads = (argc > 1) ? atoi(argv[1]) : DEFAULT_THREADS;
  loops = (argc > 2) ? atoi(argv[2]) : DEFAULT_LOOPS;
  allocs_per_loop = (argc > 3) ? atoi(argv[3]) : DEFAULT_ALLOCS;

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
  bench_report("rptest: threads=%d loops=%d allocs_per_loop=%d total=%ld time=%.3f sec (%.0f ops/sec)\n",
         nthreads, loops, allocs_per_loop, total_allocs, elapsed, total_allocs / elapsed);

  free(threads);
  free(args);

  return 0;
}
