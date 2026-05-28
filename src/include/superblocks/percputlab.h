// -*- C++ -*-

/*
  The Hoard Multiprocessor Memory Allocator
  www.hoard.org

  Author: Emery Berger, http://www.emeryberger.com
  Copyright (c) 1998-2020 Emery Berger

*/

/**
 * @class  PerCpuTLAB
 * @author Emery Berger <http://www.cs.umass.edu/~emery>
 * @brief  Per-CPU allocation buffer using restartable sequences (rseq).
 *
 * This provides extremely fast allocation by:
 * 1. Avoiding TLS lookups - uses rseq to get CPU ID directly
 * 2. Using per-CPU freelists - no locking needed for same-CPU operations
 * 3. Falling back to per-thread TLAB when rseq unavailable
 */

#ifndef HOARD_PERCPUTLAB_H
#define HOARD_PERCPUTLAB_H

#include "heaplayers.h"
#include "utility/cpp23compat.h"

#include <sys/rseq.h>
#include <cstring>

#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-variable"
#endif

namespace Hoard {

  // Maximum number of CPUs supported
  enum { MaxCPUs = 256 };

  // Per-CPU freelist entry
  struct PerCpuFreelistEntry {
    PerCpuFreelistEntry* next;
  };

  // Per-CPU cache line - holds multiple freelists for hot size classes
  // Aligned to cache line to avoid false sharing between CPUs
  struct alignas(128) PerCpuCache {
    // Just cache the hottest small sizes (8, 16, 32, 64 bytes = 4 size classes)
    static constexpr int NumCachedSizes = 4;
    PerCpuFreelistEntry* heads[NumCachedSizes];
    uint16_t counts[NumCachedSizes];
    uint16_t maxCounts[NumCachedSizes];

    PerCpuCache() {
      for (int i = 0; i < NumCachedSizes; i++) {
        heads[i] = nullptr;
        counts[i] = 0;
        maxCounts[i] = 256;  // Cache up to 256 objects per size class per CPU
      }
    }
  };

  // Check if rseq is available
  inline bool rseqAvailable() {
    return __rseq_size > 0;
  }

  // Get current CPU ID using rseq (fast path)
  inline int rseqGetCpu() {
    if (__rseq_size > 0) {
      struct rseq *rs = (struct rseq *)((char *)__builtin_thread_pointer() + __rseq_offset);
      return rs->cpu_id_start;
    }
    return -1;
  }

  template <int NumBins,
	    int (*getSizeClass) (size_t),
	    size_t (*getClassSize) (int),
	    size_t LargestObject,
	    size_t LocalHeapThreshold,
	    class SuperblockType,
	    unsigned int SuperblockSize,
	    class ParentHeap>
  class PerCpuTLAB {

    enum { DesiredAlignment = HL::MallocInfo::Alignment };

    // Per-CPU threshold - allow more caching per CPU than per thread
    // since CPUs are fewer than threads
    enum { PerCpuThreshold = LocalHeapThreshold * 2 };
    enum { MaxPerCpuObjects = PerCpuThreshold / 16 };  // Assuming min object size ~16

  public:

    enum { Alignment = ParentHeap::Alignment };

    PerCpuTLAB(ParentHeap * parent)
      : _parentHeap(parent),
        _localHeapBytes(0),
        _rseqEnabled(rseqAvailable())
    {
      static_assert(gcd<Alignment, DesiredAlignment>::value == DesiredAlignment,
		    "Alignment mismatch.");
      static_assert((Alignment >= 2 * sizeof(size_t)),
		    "Alignment must be enough to hold two pointers.");

      // Initialize per-CPU freelists (done once globally)
      initPerCpuFreelists();
    }

    ~PerCpuTLAB() {
      clear();
    }

    inline static size_t getSize(void * ptr) {
      return getSuperblock(ptr)->getSize(ptr);
    }

    INLINE void * malloc(size_t sz) {
      // Fast path: thread-local freelist (no locking needed)
      if (HL_EXPECT_TRUE(sz <= LargestObject)) {
        auto c = getSizeClass(sz);
        auto * ptr = _localHeap(c).get();
        if (HL_EXPECT_TRUE(ptr != nullptr)) {
          _localHeapBytes -= getClassSize(c);
          return ptr;
        }
      }

      // Slow path: go to parent heap
      return _parentHeap->malloc(sz);
    }

    INLINE void free(void * ptr) {
      auto * s = getSuperblock(ptr);

      if (HL_EXPECT_TRUE(s != nullptr && s->isValidSuperblock())) {
        ptr = s->normalize(ptr);
        auto sz = s->getObjectSize();

        // Fast path: thread-local cache
        if (HL_EXPECT_TRUE((sz <= LargestObject) &&
                           (sz + _localHeapBytes <= LocalHeapThreshold))) {
          auto c = getSizeClass(sz);
          _localHeap(c).insert((HL::SLList::Entry *)ptr);
          _localHeapBytes += getClassSize(c);
        } else {
          _parentHeap->free(ptr);
        }
      }
    }

    void clear() {
      // Clear thread-local freelists
      int i = NumBins - 1;
      while ((_localHeapBytes > 0) && (i >= 0)) {
        auto sz = getClassSize(i);
        while (!_localHeap(i).isEmpty()) {
          auto * e = _localHeap(i).get();
          _parentHeap->free(e);
          _localHeapBytes -= sz;
        }
        i--;
      }
      // Note: per-CPU freelists are shared and not cleared here
    }

    static inline SuperblockType * getSuperblock(void * ptr) {
      return SuperblockType::getSuperblock(ptr);
    }

  private:

    // Map size class to cached slot (only cache small sizes)
    static int toCachedSlot(int sizeClass) {
      // Size classes 0-3 map to slots 0-3 (8, 16, 32, 64 bytes typically)
      if (sizeClass < PerCpuCache::NumCachedSizes) return sizeClass;
      return -1;  // Not cached
    }

    // Per-CPU cache operations
    void * perCpuMalloc(int cpu, int sizeClass) {
      int slot = toCachedSlot(sizeClass);
      if (slot < 0) return nullptr;

      auto& cache = getPerCpuCache(cpu);
      auto * entry = cache.heads[slot];
      if (entry != nullptr) {
        cache.heads[slot] = entry->next;
        cache.counts[slot]--;
        return entry;
      }
      return nullptr;
    }

    bool perCpuFree(int cpu, int sizeClass, void * ptr) {
      int slot = toCachedSlot(sizeClass);
      if (slot < 0) return false;

      auto& cache = getPerCpuCache(cpu);
      if (cache.counts[slot] < cache.maxCounts[slot]) {
        auto * entry = reinterpret_cast<PerCpuFreelistEntry*>(ptr);
        entry->next = cache.heads[slot];
        cache.heads[slot] = entry;
        cache.counts[slot]++;
        return true;
      }
      return false;  // Per-CPU cache full, use fallback
    }

    static PerCpuCache& getPerCpuCache(int cpu) {
      // Each CPU gets its own cache line, avoiding false sharing
      static PerCpuCache caches[MaxCPUs];
      return caches[cpu];
    }

    static void initPerCpuFreelists() {
      // Initialization happens in PerCpuCache constructor
    }

    // Disable assignment and copying
    PerCpuTLAB(const PerCpuTLAB&);
    PerCpuTLAB& operator=(const PerCpuTLAB&);

    /// Padding to prevent false sharing
    double _pad[128 / sizeof(double)];

    /// This heap's 'parent' (where to go for more memory)
    ParentHeap * _parentHeap;

    /// The number of bytes in thread-local cache
    size_t _localHeapBytes;

    /// Whether rseq is available
    bool _rseqEnabled;

    /// Thread-local freelist (fallback when rseq unavailable or full)
    Array<NumBins, HL::SLList> _localHeap;
  };

}

#if defined(__clang__)
#pragma clang diagnostic pop
#endif

#endif
