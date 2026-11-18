// Threads allocate+free a small-object working set and exit normally.
// RSS must plateau, not grow per round. Uses the plain pthread_create the
// program binds (pthread_create@GLIBC_2.34 on glibc >= 2.34).
#define _GNU_SOURCE
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static long rss_kb(void) { FILE* f = fopen("/proc/self/status","r"); char l[256]; long v=0;
  while (fgets(l,sizeof l,f)) if (!strncmp(l,"VmRSS:",6)) v = atol(l+6); fclose(f); return v; }
enum { N = (64 << 20) / 64 };
static void* work(void* arg) { (void)arg;
  void** v = malloc(N * sizeof *v);
  for (int i = 0; i < N; i++) { v[i] = malloc(64); memset(v[i], 1, 64); }
  for (int i = 0; i < N; i++) free(v[i]);
  free(v); return NULL; }
int main(int argc, char** argv) {
  int T = argc > 1 ? atoi(argv[1]) : 8, R = argc > 2 ? atoi(argv[2]) : 20;
  long first = 0, last = 0;
  for (int r = 0; r < R; r++) {
    pthread_t t[256];
    for (int i = 0; i < T; i++) pthread_create(&t[i], NULL, work, NULL);
    for (int i = 0; i < T; i++) pthread_join(t[i], NULL);
    last = rss_kb(); if (r == 1) first = last;
    if (r % 5 == 0 || r == R-1) printf("round %2d RSS %ld kB\n", r, last);
  }
  long growth = last - first;
  printf("%s threadchurn T=%d: RSS growth rounds 1..%d = %ld kB\n", growth > 65536 ? "FAIL" : "ok  ", T, R-1, growth);
  return growth > 65536;
}
