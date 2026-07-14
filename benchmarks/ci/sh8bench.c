/*
 * sh8bench.c - SmartHeap-style multi-threaded stress test
 *
 * Based on MicroQuill's sh8bench. Multi-threaded stress test where some
 * objects are freed by other threads (like larson) and some objects
 * freed in reverse order (like sh6bench).
 *
 * Usage: sh8bench [threads] [iterations]
 */

#include "bench_common.h"

#define DEFAULT_THREADS 4
#define DEFAULT_ITERATIONS 500000
#define SHARED_PTRS 5000
#define LOCAL_PTRS 1000
#define MAX_SIZE 500

static void** shared_ptrs;
static bench_mutex_t lock;
static volatile long total_ops = 0;
static int iterations_per_thread;

typedef struct {
  int tid;
  unsigned int seed;
} thread_arg_t;

static BENCH_THREAD_RETURN worker(void* arg) {
  thread_arg_t* ta = (thread_arg_t*)arg;
  unsigned int seed = ta->seed;

  /* Thread-local allocations */
  void** local = (void**)calloc(LOCAL_PTRS, sizeof(void*));

  for (int i = 0; i < iterations_per_thread; i++) {
    int action = bench_rand(&seed) % 4;

    if (action == 0) {
      /* Allocate/free in shared array (cross-thread frees) */
      int idx = bench_rand(&seed) % SHARED_PTRS;
      size_t size = (bench_rand(&seed) % MAX_SIZE) + 1;

      void* newptr = malloc(size);
      if (newptr) memset(newptr, (char)ta->tid, size);

      bench_mutex_lock(&lock);
      void* old = shared_ptrs[idx];
      shared_ptrs[idx] = newptr;
      bench_mutex_unlock(&lock);

      if (old) free(old);
    }
    else if (action == 1) {
      /* Allocate in local array */
      int idx = bench_rand(&seed) % LOCAL_PTRS;
      if (local[idx]) free(local[idx]);
      size_t size = (bench_rand(&seed) % MAX_SIZE) + 1;
      local[idx] = malloc(size);
      if (local[idx]) memset(local[idx], (char)i, size);
    }
    else if (action == 2) {
      /* Free in reverse order (stress test) */
      int idx1 = bench_rand(&seed) % LOCAL_PTRS;
      int idx2 = bench_rand(&seed) % LOCAL_PTRS;
      if (local[idx1] && local[idx2] && idx1 != idx2) {
        /* Free idx2 before idx1 even if idx1 was allocated first */
        free(local[idx2]);
        local[idx2] = NULL;
        free(local[idx1]);
        local[idx1] = NULL;
      }
    }
    else {
      /* Realloc in local array */
      int idx = bench_rand(&seed) % LOCAL_PTRS;
      size_t size = (bench_rand(&seed) % MAX_SIZE) + 1;
      void* p = realloc(local[idx], size);
      if (p) {
        local[idx] = p;
        ((char*)p)[0] = (char)i;
      }
    }

    bench_atomic_inc(&total_ops);
  }

  /* Cleanup local */
  for (int i = 0; i < LOCAL_PTRS; i++) {
    if (local[i]) free(local[i]);
  }
  free(local);

  return BENCH_THREAD_RETURN_VALUE;
}

int main(int argc, char** argv) {
  int nthreads = (argc > 1) ? atoi(argv[1]) : DEFAULT_THREADS;
  int total_iterations = (argc > 2) ? atoi(argv[2]) : DEFAULT_ITERATIONS * nthreads;
  iterations_per_thread = total_iterations / nthreads;

  bench_timer_t start;
  bench_thread_t* threads = (bench_thread_t*)malloc(sizeof(bench_thread_t) * nthreads);
  thread_arg_t* args = (thread_arg_t*)malloc(sizeof(thread_arg_t) * nthreads);

  bench_mutex_init(&lock);
  shared_ptrs = (void**)calloc(SHARED_PTRS, sizeof(void*));

  bench_timer_init();
  bench_timer_start(&start);

  for (int i = 0; i < nthreads; i++) {
    args[i].tid = i;
    args[i].seed = i + 1;
    bench_thread_create(&threads[i], worker, &args[i]);
  }

  bench_thread_join_all(threads, nthreads);

  double elapsed = bench_timer_elapsed(&start);
  bench_report("sh8bench: threads=%d ops=%ld time=%.3f sec (%.0f ops/sec)\n",
         nthreads, total_ops, elapsed, total_ops / elapsed);

  /* Cleanup shared */
  for (int i = 0; i < SHARED_PTRS; i++) {
    if (shared_ptrs[i]) free(shared_ptrs[i]);
  }
  free(shared_ptrs);
  free(threads);
  free(args);
  bench_mutex_destroy(&lock);

  return 0;
}
