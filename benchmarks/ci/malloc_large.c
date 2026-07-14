/*
 * malloc_large.c - Test large allocations (64KB - 8MB)
 *
 * Tests allocator performance with large allocations that may use
 * different code paths (e.g., mmap vs sbrk).
 *
 * Usage: malloc_large [iterations]
 */

#include "bench_common.h"

#define DEFAULT_ITERATIONS 10000
#define MIN_SIZE (64 * 1024)       /* 64 KB */
#define MAX_SIZE (8 * 1024 * 1024) /* 8 MB */

int main(int argc, char** argv) {
  int iterations = (argc > 1) ? atoi(argv[1]) : DEFAULT_ITERATIONS;
  bench_timer_t start;
  unsigned int seed = 42;

  bench_timer_init();
  bench_timer_start(&start);

  for (int i = 0; i < iterations; i++) {
    size_t size = MIN_SIZE + (bench_rand(&seed) % (MAX_SIZE - MIN_SIZE));
    char* p = (char*)malloc(size);
    if (p) {
      p[0] = 1;
      p[size / 2] = 2;
      p[size - 1] = 3;
      free(p);
    }
  }

  double elapsed = bench_timer_elapsed(&start);
  bench_report("malloc-large: iterations=%d time=%.3f sec (%.0f ops/sec)\n",
         iterations, elapsed, iterations / elapsed);

  return 0;
}
