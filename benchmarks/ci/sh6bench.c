/*
 * sh6bench.c - SmartHeap-style stress test (single-threaded)
 *
 * Based on MicroQuill's sh6bench. Stress test where some objects are freed
 * in LIFO order (last-allocated, first-freed), but others are freed in
 * reverse order to stress the allocator.
 *
 * Usage: sh6bench [iterations]
 */

#include "bench_common.h"

#define DEFAULT_ITERATIONS 1000000
#define MAX_PTRS 5000
#define MAX_SIZE 500

int main(int argc, char** argv) {
  int iterations = (argc > 1) ? atoi(argv[1]) : DEFAULT_ITERATIONS;
  bench_timer_t start;
  unsigned int seed = 12345;

  void** ptrs = (void**)calloc(MAX_PTRS, sizeof(void*));
  int* sizes = (int*)calloc(MAX_PTRS, sizeof(int));

  bench_timer_init();
  bench_timer_start(&start);

  for (int i = 0; i < iterations; i++) {
    int idx = bench_rand(&seed) % MAX_PTRS;

    if (ptrs[idx]) {
      /* Free existing - sometimes in reverse order */
      if ((bench_rand(&seed) % 4) == 0) {
        /* Reverse order: find another allocated block and free it instead */
        int other = (idx + bench_rand(&seed) % 100) % MAX_PTRS;
        if (ptrs[other]) {
          free(ptrs[other]);
          ptrs[other] = NULL;
        }
      }
      free(ptrs[idx]);
      ptrs[idx] = NULL;
    }

    /* Allocate new */
    int size = (bench_rand(&seed) % MAX_SIZE) + 1;
    ptrs[idx] = malloc(size);
    if (ptrs[idx]) {
      memset(ptrs[idx], (char)i, size);
      sizes[idx] = size;
    }
  }

  /* Cleanup */
  for (int i = 0; i < MAX_PTRS; i++) {
    if (ptrs[i]) free(ptrs[i]);
  }

  double elapsed = bench_timer_elapsed(&start);
  bench_report("sh6bench: iterations=%d time=%.3f sec (%.0f ops/sec)\n",
         iterations, elapsed, iterations / elapsed);

  free(ptrs);
  free(sizes);

  return 0;
}
