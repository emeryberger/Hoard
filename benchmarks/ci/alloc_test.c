/*
 * alloc_test.c - Intensive allocation workload with Pareto size distribution
 *
 * Based on OLogN Technologies' alloc-test benchmark. Tests allocation
 * performance with many threads and various object sizes.
 *
 * Usage: alloc_test [threads] [iterations]
 */

#include "bench_common.h"

#define DEFAULT_THREADS 4
#define DEFAULT_ITERATIONS 10000000
#define MAX_PTRS 1000

static volatile long total_allocs = 0;
static int iterations_per_thread;

typedef struct {
  int tid;
  unsigned int seed;
} thread_arg_t;

static BENCH_THREAD_RETURN worker(void* arg) {
  thread_arg_t* ta = (thread_arg_t*)arg;
  unsigned int seed = ta->seed;
  void* ptrs[MAX_PTRS];
  memset(ptrs, 0, sizeof(ptrs));

  for (int i = 0; i < iterations_per_thread; i++) {
    int idx = bench_rand(&seed) % MAX_PTRS;
    size_t size = (bench_rand(&seed) % 1024) + 1;

    if (ptrs[idx]) free(ptrs[idx]);
    ptrs[idx] = malloc(size);
    if (ptrs[idx]) ((char*)ptrs[idx])[0] = (char)i;

    bench_atomic_inc(&total_allocs);
  }

  for (int i = 0; i < MAX_PTRS; i++) {
    if (ptrs[i]) free(ptrs[i]);
  }

  return BENCH_THREAD_RETURN_VALUE;
}

int main(int argc, char** argv) {
  int nthreads = (argc > 1) ? atoi(argv[1]) : DEFAULT_THREADS;
  int total_iterations = (argc > 2) ? atoi(argv[2]) : DEFAULT_ITERATIONS;
  iterations_per_thread = total_iterations / nthreads;

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
  printf("alloc-test: threads=%d allocs=%ld time=%.3f sec (%.0f ops/sec)\n",
         nthreads, total_allocs, elapsed, total_allocs / elapsed);

  free(threads);
  free(args);

  return 0;
}
