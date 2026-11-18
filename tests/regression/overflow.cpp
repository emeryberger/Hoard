// Size-overflow boundary probes: every request below must FAIL (NULL / nonzero rc / bad_alloc).
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <cerrno>
#include <new>
#include <initializer_list>
#include <malloc.h>
static int fails = 0;
#define EXPECT_NULL(expr) do { errno = 0; void* p_ = (void*)(expr); \
  if (p_) { printf("FAIL %-44s -> %p usable=%zu\n", #expr, p_, malloc_usable_size(p_)); fails++; } \
  else printf("ok   %-44s -> NULL errno=%d\n", #expr, errno); } while (0)
int main() {
  void* keep = malloc(64);                // keep heap initialized
  for (size_t d : std::initializer_list<size_t>{1, 15, 16, 64, 255, 256, 4096}) {
    EXPECT_NULL(malloc(SIZE_MAX - d));
    EXPECT_NULL(memalign(4096, SIZE_MAX - d));
    EXPECT_NULL(memalign(16, SIZE_MAX - d));
    EXPECT_NULL(memalign(1u << 20, SIZE_MAX - d));
    EXPECT_NULL(aligned_alloc(64, SIZE_MAX - d - (SIZE_MAX - d) % 64));
    EXPECT_NULL(valloc(SIZE_MAX - d));
    EXPECT_NULL(pvalloc(SIZE_MAX - d));
    EXPECT_NULL(realloc(keep, SIZE_MAX - d));
    void* pm = nullptr; int rc = posix_memalign(&pm, 4096, SIZE_MAX - d);
    if (rc == 0) { printf("FAIL posix_memalign(4096, SIZE_MAX-%zu)\n", d); fails++; }
    try { void* q = ::operator new(SIZE_MAX - d, std::align_val_t(4096));
          printf("FAIL aligned new SIZE_MAX-%zu -> %p\n", d, q); fails++; } catch (std::bad_alloc&) {}
    try { void* q = ::operator new(SIZE_MAX - d);
          printf("FAIL new SIZE_MAX-%zu -> %p\n", d, q); fails++; } catch (std::bad_alloc&) {}
  }
  EXPECT_NULL(calloc(SIZE_MAX / 2 + 2, 2));
  EXPECT_NULL(reallocarray(keep, SIZE_MAX / 2 + 2, 2));
  void* pz = pvalloc(0);
  printf("%s pvalloc(0) usable=%zu\n", pz && malloc_usable_size(pz) >= 4096 ? "ok  " : "NOTE", pz ? malloc_usable_size(pz) : 0);
  free(keep);
  printf("overflow: %d failures\n", fails);
  return fails != 0;
}
