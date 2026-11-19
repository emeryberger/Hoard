// Confirms that a binary built like the benchmarks (cc_binary with
// malloc = "//tcmalloc") really allocates with TCMalloc, and whether its
// per-CPU caches (the mode TCMalloc is designed to run in) are active.
#include <cstdio>
#include <cstdlib>
#include <thread>
#include <vector>

#include "tcmalloc/malloc_extension.h"

int main() {
  // Touch the allocator from a few threads so per-CPU caches initialize.
  std::vector<std::thread> ts;
  for (int i = 0; i < 4; i++) ts.emplace_back([] { free(malloc(64)); });
  for (auto& t : ts) t.join();

  void* p = malloc(16);
  auto owned = tcmalloc::MallocExtension::GetOwnership(p);
  auto sz = tcmalloc::MallocExtension::GetAllocatedSize(p);
  auto bytes =
      tcmalloc::MallocExtension::GetNumericProperty("generic.current_allocated_bytes");
  std::printf("probe: malloc(16) owned_by_tcmalloc=%s allocated_size=%zu\n",
              owned == tcmalloc::MallocExtension::Ownership::kOwned ? "yes" : "NO",
              sz.value_or(0));
  std::printf("probe: per_cpu_caches_active=%s current_allocated_bytes=%zu\n",
              tcmalloc::MallocExtension::PerCpuCachesActive() ? "yes" : "NO",
              bytes.value_or(0));
  free(p);
  return owned == tcmalloc::MallocExtension::Ownership::kOwned ? 0 : 1;
}
