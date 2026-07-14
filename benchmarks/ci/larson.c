/*
 * larson.c - Server workload simulation with cross-thread frees
 *
 * Based on the Larson & Krishnan benchmark. Simulates server workloads where
 * objects are allocated by one thread and freed by another ("bleeding").
 *
 * Usage: larson [threads] [iterations] [objects]
 */

#include "bench_common.h"

#define DEFAULT_THREADS 4
#define DEFAULT_ITERATIONS 100000
#define DEFAULT_OBJECTS 10000

static void** shared_ptrs;
static bench_mutex_t lock;
static volatile long total_ops = 0;
static int num_objects;
static int iterations_per_thread;

typedef struct {
  int tid;
  unsigned int seed;
} thread_arg_t;

static BENCH_THREAD_RETURN worker(void* arg) {
  thread_arg_t* ta = (thread_arg_t*)arg;
  unsigned int seed = ta->seed;

  for (int i = 0; i < iterations_per_thread; i++) {
    int idx = bench_rand(&seed) % num_objects;
    size_t size = (bench_rand(&seed) % 1024) + 8;

    void* newptr = malloc(size);
    if (newptr) memset(newptr, 0xCD, size);

    bench_mutex_lock(&lock);
    void* old = shared_ptrs[idx];
    shared_ptrs[idx] = newptr;
    bench_mutex_unlock(&lock);

    if (old) free(old);

    bench_atomic_inc(&total_ops);
  }

  return BENCH_THREAD_RETURN_VALUE;
}

int main(int argc, char** argv) {
  int nthreads = (argc > 1) ? atoi(argv[1]) : DEFAULT_THREADS;
  int total_iterations = (argc > 2) ? atoi(argv[2]) : DEFAULT_ITERATIONS * nthreads;
  num_objects = (argc > 3) ? atoi(argv[3]) : DEFAULT_OBJECTS;
  iterations_per_thread = total_iterations / nthreads;

  bench_timer_t start;
  bench_thread_t* threads = (bench_thread_t*)malloc(sizeof(bench_thread_t) * nthreads);
  thread_arg_t* args = (thread_arg_t*)malloc(sizeof(thread_arg_t) * nthreads);

  bench_mutex_init(&lock);
  shared_ptrs = (void**)calloc(num_objects, sizeof(void*));

  bench_timer_init();
  bench_timer_start(&start);

  for (int i = 0; i < nthreads; i++) {
    args[i].tid = i;
    args[i].seed = i + 1;
    bench_thread_create(&threads[i], worker, &args[i]);
  }

  bench_thread_join_all(threads, nthreads);

  double elapsed = bench_timer_elapsed(&start);
  bench_report("larson: threads=%d ops=%ld time=%.3f sec (%.0f ops/sec)\n",
         nthreads, total_ops, elapsed, total_ops / elapsed);

  /* Cleanup */
  for (int i = 0; i < num_objects; i++) {
    if (shared_ptrs[i]) free(shared_ptrs[i]);
  }
  free(shared_ptrs);
  free(threads);
  free(args);
  bench_mutex_destroy(&lock);

  return 0;
}
