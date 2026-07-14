/*
 * bench_common.h - Cross-platform threading and timing primitives
 *
 * Supports: Linux, macOS, Windows
 */

#ifndef BENCH_COMMON_H
#define BENCH_COMMON_H

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdint.h>

#ifdef _WIN32
  #define WIN32_LEAN_AND_MEAN
  #include <windows.h>

  typedef HANDLE bench_thread_t;
  typedef CRITICAL_SECTION bench_mutex_t;
  typedef DWORD (WINAPI *bench_thread_func_t)(LPVOID);

  static inline int bench_thread_create(bench_thread_t* t, bench_thread_func_t func, void* arg) {
    *t = CreateThread(NULL, 0, func, arg, 0, NULL);
    return (*t == NULL) ? -1 : 0;
  }

  static inline void bench_thread_join(bench_thread_t t) {
    WaitForSingleObject(t, INFINITE);
    CloseHandle(t);
  }

  static inline void bench_thread_join_all(bench_thread_t* threads, int n) {
    WaitForMultipleObjects(n, threads, TRUE, INFINITE);
    for (int i = 0; i < n; i++) CloseHandle(threads[i]);
  }

  static inline void bench_mutex_init(bench_mutex_t* m) { InitializeCriticalSection(m); }
  static inline void bench_mutex_destroy(bench_mutex_t* m) { DeleteCriticalSection(m); }
  static inline void bench_mutex_lock(bench_mutex_t* m) { EnterCriticalSection(m); }
  static inline void bench_mutex_unlock(bench_mutex_t* m) { LeaveCriticalSection(m); }

  static inline int64_t bench_atomic_inc(volatile long* v) { return InterlockedIncrement(v); }
  static inline void* bench_atomic_exchange_ptr(void* volatile* p, void* val) {
    return InterlockedExchangePointer(p, val);
  }

  typedef LARGE_INTEGER bench_timer_t;
  static int64_t bench_timer_freq;

  static inline void bench_timer_init(void) {
    LARGE_INTEGER freq;
    QueryPerformanceFrequency(&freq);
    bench_timer_freq = freq.QuadPart;
  }

  static inline void bench_timer_start(bench_timer_t* t) { QueryPerformanceCounter(t); }

  static inline double bench_timer_elapsed(bench_timer_t* start) {
    LARGE_INTEGER end;
    QueryPerformanceCounter(&end);
    return (double)(end.QuadPart - start->QuadPart) / bench_timer_freq;
  }

  static inline int bench_get_num_cpus(void) {
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    return si.dwNumberOfProcessors;
  }

  #define BENCH_THREAD_RETURN DWORD WINAPI
  #define BENCH_THREAD_RETURN_VALUE 0

#else
  #include <pthread.h>
  #include <unistd.h>
  #include <sys/time.h>

  #if defined(__APPLE__)
    #include <sys/sysctl.h>
  #endif

  typedef pthread_t bench_thread_t;
  typedef pthread_mutex_t bench_mutex_t;
  typedef void* (*bench_thread_func_t)(void*);

  static inline int bench_thread_create(bench_thread_t* t, bench_thread_func_t func, void* arg) {
    return pthread_create(t, NULL, func, arg);
  }

  static inline void bench_thread_join(bench_thread_t t) {
    pthread_join(t, NULL);
  }

  static inline void bench_thread_join_all(bench_thread_t* threads, int n) {
    for (int i = 0; i < n; i++) pthread_join(threads[i], NULL);
  }

  static inline void bench_mutex_init(bench_mutex_t* m) { pthread_mutex_init(m, NULL); }
  static inline void bench_mutex_destroy(bench_mutex_t* m) { pthread_mutex_destroy(m); }
  static inline void bench_mutex_lock(bench_mutex_t* m) { pthread_mutex_lock(m); }
  static inline void bench_mutex_unlock(bench_mutex_t* m) { pthread_mutex_unlock(m); }

  static inline int64_t bench_atomic_inc(volatile long* v) {
    return __sync_add_and_fetch(v, 1);
  }
  static inline void* bench_atomic_exchange_ptr(void* volatile* p, void* val) {
    return __sync_lock_test_and_set(p, val);
  }

  typedef struct timeval bench_timer_t;

  static inline void bench_timer_init(void) { /* no-op on Unix */ }

  static inline void bench_timer_start(bench_timer_t* t) {
    gettimeofday(t, NULL);
  }

  static inline double bench_timer_elapsed(bench_timer_t* start) {
    struct timeval end;
    gettimeofday(&end, NULL);
    return (end.tv_sec - start->tv_sec) + (end.tv_usec - start->tv_usec) / 1000000.0;
  }

  static inline int bench_get_num_cpus(void) {
    #if defined(__APPLE__)
      int nm[2] = { CTL_HW, HW_AVAILCPU };
      int count = 0;
      size_t len = sizeof(count);
      sysctl(nm, 2, &count, &len, NULL, 0);
      return count > 0 ? count : 1;
    #else
      long n = sysconf(_SC_NPROCESSORS_ONLN);
      return n > 0 ? (int)n : 1;
    #endif
  }

  #define BENCH_THREAD_RETURN void*
  #define BENCH_THREAD_RETURN_VALUE NULL

#endif

/* Simple random number generator (thread-safe) */
static inline unsigned int bench_rand(unsigned int* seed) {
  *seed = *seed * 1103515245 + 12345;
  return (*seed >> 16) & 0x7fff;
}

/*
 * Emit a benchmark's result line.
 *
 * Prints to stdout as usual, and ALSO appends to the file named by BENCH_OUT
 * when that variable is set.
 *
 * Why: on Windows the benchmarks run under DLL injection (withdll.exe), and
 * their stdout was not being captured -- CI could see exit codes but never a
 * number, so Windows could be crash-tested and never performance-tested. A file
 * we open, write and fclose ourselves does not depend on how the injected
 * process inherited its standard handles, nor on the CRT flushing a buffer
 * during teardown, so the result survives either way.
 */
#include <stdarg.h>

static inline void bench_report(const char* fmt, ...) {
  va_list ap;

  va_start(ap, fmt);
  vprintf(fmt, ap);
  va_end(ap);
  fflush(stdout);

  const char* path = getenv("BENCH_OUT");
  if (path != NULL && path[0] != '\0') {
    FILE* f = fopen(path, "a");
    if (f != NULL) {
      va_start(ap, fmt);
      vfprintf(f, fmt, ap);
      va_end(ap);
      fclose(f);            /* explicit: the line is on disk before we exit */
    }
  }
}

#endif /* BENCH_COMMON_H */
