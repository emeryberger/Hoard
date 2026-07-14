/*
 * xmalloc_test.c - Asymmetric producer/consumer allocation pattern
 *
 * Based on the xmalloc-test benchmark. Tests allocators with purely
 * allocating threads and purely deallocating threads with objects
 * migrating between them.
 *
 * Usage: xmalloc_test [num_producer_consumer_pairs] [iterations]
 */

#include "bench_common.h"

#define DEFAULT_PAIRS 2
#define DEFAULT_ITERATIONS 500000
#define QUEUE_SIZE 10000

static void* queue[QUEUE_SIZE];
static bench_mutex_t queue_lock;
static volatile long head = 0;
static volatile long tail = 0;
static volatile long running = 1;
static volatile long produced = 0;
static volatile long consumed = 0;
static int target_produced;

typedef struct {
  unsigned int seed;
} thread_arg_t;

static BENCH_THREAD_RETURN producer(void* arg) {
  thread_arg_t* ta = (thread_arg_t*)arg;
  unsigned int seed = ta->seed;

  while (produced < target_produced) {
    size_t size = (bench_rand(&seed) % 512) + 8;
    void* p = malloc(size);
    if (!p) continue;
    memset(p, 0xAB, size);

    bench_mutex_lock(&queue_lock);
    long h = head;
    long next = (h + 1) % QUEUE_SIZE;
    if (next != tail) {
      queue[h] = p;
      head = next;
      bench_atomic_inc(&produced);
      p = NULL;
    }
    bench_mutex_unlock(&queue_lock);

    if (p) free(p); /* Queue was full */
  }

  return BENCH_THREAD_RETURN_VALUE;
}

static BENCH_THREAD_RETURN consumer(void* arg) {
  (void)arg;

  while (running || tail != head) {
    void* p = NULL;

    bench_mutex_lock(&queue_lock);
    long t = tail;
    if (t != head) {
      p = queue[t];
      queue[t] = NULL;
      tail = (t + 1) % QUEUE_SIZE;
    }
    bench_mutex_unlock(&queue_lock);

    if (p) {
      free(p);
      bench_atomic_inc(&consumed);
    }
  }

  return BENCH_THREAD_RETURN_VALUE;
}

int main(int argc, char** argv) {
  int pairs = (argc > 1) ? atoi(argv[1]) : DEFAULT_PAIRS;
  target_produced = (argc > 2) ? atoi(argv[2]) : DEFAULT_ITERATIONS;

  int num_producers = pairs;
  int num_consumers = pairs;
  int total = num_producers + num_consumers;

  bench_timer_t start;
  bench_thread_t* threads = (bench_thread_t*)malloc(sizeof(bench_thread_t) * total);
  thread_arg_t* args = (thread_arg_t*)malloc(sizeof(thread_arg_t) * num_producers);

  bench_mutex_init(&queue_lock);

  bench_timer_init();
  bench_timer_start(&start);

  /* Start consumers first */
  for (int i = 0; i < num_consumers; i++) {
    bench_thread_create(&threads[num_producers + i], consumer, NULL);
  }

  /* Start producers */
  for (int i = 0; i < num_producers; i++) {
    args[i].seed = i + 1;
    bench_thread_create(&threads[i], producer, &args[i]);
  }

  /* Wait for producers to finish */
  for (int i = 0; i < num_producers; i++) {
    bench_thread_join(threads[i]);
  }

  running = 0;

  /* Wait for consumers to finish */
  for (int i = 0; i < num_consumers; i++) {
    bench_thread_join(threads[num_producers + i]);
  }

  double elapsed = bench_timer_elapsed(&start);
  bench_report("xmalloc-test: producers=%d consumers=%d produced=%ld consumed=%ld time=%.3f sec\n",
         num_producers, num_consumers, produced, consumed, elapsed);

  free(threads);
  free(args);
  bench_mutex_destroy(&queue_lock);

  return 0;
}
