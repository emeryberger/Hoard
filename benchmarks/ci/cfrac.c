/*
 * cfrac.c - Single-threaded allocation stress test
 *
 * Simulates allocation-heavy computation similar to continued fraction
 * factorization. Tests single-threaded malloc/free performance.
 *
 * Usage: cfrac [iterations]
 */

#include "bench_common.h"

#define DEFAULT_ITERATIONS 5000000

int main(int argc, char** argv) {
  int iterations = (argc > 1) ? atoi(argv[1]) : DEFAULT_ITERATIONS;
  bench_timer_t start;
  unsigned int seed = 12345;

  bench_timer_init();
  bench_timer_start(&start);

  for (int i = 0; i < iterations; i++) {
    size_t size = (bench_rand(&seed) % 256) + 8;
    char* p = (char*)malloc(size);
    if (p) {
      memset(p, (char)(i & 0xFF), size);
      free(p);
    }
  }

  double elapsed = bench_timer_elapsed(&start);
  bench_report("cfrac: iterations=%d time=%.3f sec (%.0f ops/sec)\n",
         iterations, elapsed, iterations / elapsed);

  return 0;
}
