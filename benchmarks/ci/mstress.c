/*
 * mstress.c - Server-like allocation patterns with object migration
 *
 * Simulates real-world server allocation patterns where objects can
 * migrate between threads and some have long lifetimes.
 *
 * Usage: mstress [threads] [scale] [iterations]
 */

#include "bench_common.h"

#define DEFAULT_THREADS 4
#define DEFAULT_SCALE 50
#define DEFAULT_ITERATIONS 10
#define TRANSFERS 1000
#define ALLOCS_PER_ROUND 50000

static void* volatile transfer[TRANSFERS];
static volatile long total_allocs = 0;
static int scale;

typedef struct {
  int tid;
  unsigned int seed;
} thread_arg_t;

static BENCH_THREAD_RETURN worker(void* arg) {
  thread_arg_t* ta = (thread_arg_t*)arg;
  unsigned int seed = ta->seed;
  int tid = ta->tid;
  void* ptrs[100];
  memset(ptrs, 0, sizeof(ptrs));

  int allocs = ALLOCS_PER_ROUND * scale / 50;

  for (int i = 0; i < allocs; i++) {
    size_t size = (bench_rand(&seed) % 1024) + 8;
    int idx = i % 100;

    if (ptrs[idx]) free(ptrs[idx]);
    ptrs[idx] = malloc(size);
    if (ptrs[idx]) memset(ptrs[idx], (char)tid, size);

    /* Occasionally transfer to shared array */
    if ((i % 100) == 0) {
      int tidx = bench_rand(&seed) % TRANSFERS;
      void* old = bench_atomic_exchange_ptr((void* volatile*)&transfer[tidx], ptrs[idx]);
      ptrs[idx] = old;
    }

    bench_atomic_inc(&total_allocs);
  }

  for (int i = 0; i < 100; i++) {
    if (ptrs[i]) free(ptrs[i]);
  }

  return BENCH_THREAD_RETURN_VALUE;
}

int main(int argc, char** argv) {
  int nthreads = (argc > 1) ? atoi(argv[1]) : DEFAULT_THREADS;
  scale = (argc > 2) ? atoi(argv[2]) : DEFAULT_SCALE;
  int iterations = (argc > 3) ? atoi(argv[3]) : DEFAULT_ITERATIONS;

  bench_timer_t start;

  bench_timer_init();
  bench_timer_start(&start);

  /* Run multiple phases, destroying and recreating threads each time */
  for (int iter = 0; iter < iterations; iter++) {
    bench_thread_t* threads = (bench_thread_t*)malloc(sizeof(bench_thread_t) * nthreads);
    thread_arg_t* args = (thread_arg_t*)malloc(sizeof(thread_arg_t) * nthreads);

    for (int i = 0; i < nthreads; i++) {
      args[i].tid = i;
      args[i].seed = iter * nthreads + i + 1;
      bench_thread_create(&threads[i], worker, &args[i]);
    }

    bench_thread_join_all(threads, nthreads);
    free(threads);
    free(args);
  }

  double elapsed = bench_timer_elapsed(&start);
  printf("mstress: threads=%d scale=%d iterations=%d allocs=%ld time=%.3f sec (%.0f ops/sec)\n",
         nthreads, scale, iterations, total_allocs, elapsed, total_allocs / elapsed);

  /* Cleanup transfer array */
  for (int i = 0; i < TRANSFERS; i++) {
    if (transfer[i]) free((void*)transfer[i]);
  }

  return 0;
}
