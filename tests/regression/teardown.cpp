// Teardown edge cases: allocation after the TLAB key destructor ran.
#include <pthread.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
static pthread_key_t lateKey;
static void lateDtor(void* p) {            // TSD destructor that frees AND allocates
  free(p);
  void* q = malloc(100); memset(q, 3, 100); free(q);
}
struct TL { std::vector<char*> v; ~TL() { for (auto* p : v) free(p); v.clear(); v.shrink_to_fit(); char* z = (char*)malloc(77); free(z); } };
static thread_local TL tl;
static void* work(void* arg) {
  long mode = (long)arg;
  for (int i = 0; i < 2000; i++) tl.v.push_back((char*)malloc(48));
  pthread_setspecific(lateKey, malloc(32));
  void** v = (void**)malloc(200000 * sizeof(void*));
  for (int i = 0; i < 200000; i++) v[i] = malloc(64);
  for (int i = 0; i < 200000; i++) free(v[i]);
  free(v);
  if (mode == 1) pthread_exit(nullptr);
  return nullptr;
}
int main(int argc, char** argv) {
  int rounds = argc > 1 ? atoi(argv[1]) : 200;
  pthread_key_create(&lateKey, lateDtor);
  for (int r = 0; r < rounds; r++) {
    pthread_t t[8];
    for (long i = 0; i < 8; i++) pthread_create(&t[i], nullptr, work, (void*)(i & 1));
    for (int i = 0; i < 8; i++) pthread_join(t[i], nullptr);
  }
  FILE* f = fopen("/proc/self/status", "r"); char l[256];
  long rss = 0;
  while (fgets(l, sizeof l, f)) if (!strncmp(l, "VmRSS:", 6)) rss = atol(l + 6);
  fclose(f);
  // Every thread's TLAB must be flushed at exit; without that, 200 rounds of
  // 8 threads strand gigabytes (measured: ~10 GB) in dead threads' TLABs.
  printf("%s teardown: %d rounds, VmRSS %ld kB\n", rss > 1024 * 1024 ? "FAIL" : "ok  ", rounds, rss);
  return rss > 1024 * 1024;
}
