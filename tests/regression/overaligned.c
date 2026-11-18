// Large-alignment allocations: usable size, realloc data preservation, free reclaim.
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <malloc.h>
static long vm_kb(void) { FILE* f = fopen("/proc/self/status","r"); char l[256]; long v=0;
  while (fgets(l,sizeof l,f)) if (!strncmp(l,"VmSize:",7)) v = atol(l+7); fclose(f); return v; }
int main(void) {
  int fails = 0;
  size_t aligns[] = {4096, 32768, 65536, 131072, 262144, 524288, 1u<<20};
  size_t sizes[] = {1, 1023, 100000, 1u<<20};
  for (unsigned i = 0; i < sizeof aligns/sizeof *aligns; i++)
  for (unsigned j = 0; j < sizeof sizes/sizeof *sizes; j++) {
    size_t a = aligns[i], n = sizes[j];
    unsigned char* p = memalign(a, n);
    if (!p || ((uintptr_t)p & (a-1))) { printf("FAIL memalign(%zu,%zu) %p\n", a, n, p); fails++; continue; }
    size_t u = malloc_usable_size(p);
    if (u < n) { printf("FAIL usable(%zu,%zu)=%zu\n", a, n, u); fails++; }
    for (size_t k = 0; k < n; k++) p[k] = (unsigned char)(k * 31 + 7);
    unsigned char* q = realloc(p, n + 4096 + u);       // force a move
    if (!q) { printf("FAIL realloc NULL (%zu,%zu)\n", a, n); fails++; free(p); continue; }
    for (size_t k = 0; k < n; k++) if (q[k] != (unsigned char)(k * 31 + 7)) {
      printf("FAIL dataloss align=%zu n=%zu at %zu\n", a, n, k); fails++; break; }
    free(q);
  }
  // Leak check: memalign(big)+free must not grow the address space.
  for (unsigned i = 3; i < sizeof aligns/sizeof *aligns; i++) {
    for (int w = 0; w < 64; w++) free(memalign(aligns[i], 1u<<20));
    long before = vm_kb();
    for (int it = 0; it < 2000; it++) free(memalign(aligns[i], 1u<<20));
    long grow = vm_kb() - before;
    printf("%s leak align=%zu: VmSize growth %ld kB over 2000 iters\n", grow > 16384 ? "FAIL" : "ok  ", aligns[i], grow);
    if (grow > 16384) fails++;
  }
  printf("overaligned: %d failures\n", fails);
  return fails != 0;
}
