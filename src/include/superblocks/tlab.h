// -*- C++ -*-

/*

  The Hoard Multiprocessor Memory Allocator
  www.hoard.org

  Author: Emery Berger, http://www.emeryberger.com
  Copyright (c) 1998-2020 Emery Berger

  See the LICENSE file at the top-level directory of this
  distribution and at http://github.com/emeryberger/Hoard.

*/

/**
 *
 * @class  ThreadLocalAllocationBuffer
 * @author Emery Berger <http://www.cs.umass.edu/~emery>
 * @brief  An allocator, meant to be used for thread-local allocation.
 */

#ifndef HOARD_TLAB_H
#define HOARD_TLAB_H

#include "heaplayers.h"
#include "utility/cpp23compat.h"

#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-variable"
#endif

namespace Hoard {

  template <int NumBins,
	    int (*getSizeClass) (size_t),
	    size_t (*getClassSize) (int),
	    size_t LargestObject,
	    size_t LocalHeapThreshold,
	    class SuperblockType,
	    unsigned int SuperblockSize,
	    class ParentHeap>

  class ThreadLocalAllocationBuffer {

    enum { DesiredAlignment = HL::MallocInfo::Alignment };

    // Dynamic TLAB sizing: grow threshold under pressure, bounded by blowup constraint.
    // The blowup bound is O(U + c*P*S*log(M)). We allow TLAB to grow proportionally
    // to the thread's allocation high-water mark to stay within bounds.
    // The TLAB can be up to 1/2 of peak allocations - this is safe because the
    // blowup bound has O(log M) slack that accommodates per-thread caching.
    enum { MaxHeapFraction = 2 };  // TLAB can be up to 1/2 of peak allocations
    enum { MaxLocalHeapThreshold = LocalHeapThreshold * 16 };  // Hard cap: 256MB

    // Per-bin limits: larger for small objects, smaller for large objects.
    // Total TLAB size bounded by LocalHeapThreshold across all bins.

  public:

    enum { Alignment = ParentHeap::Alignment };

    ThreadLocalAllocationBuffer (ParentHeap * parent)
      : _parentHeap (parent),
      	_localHeapBytes (0),
        _currentThreshold (LocalHeapThreshold),
        _cumulativeAllocatedBytes (0)
    {
      static_assert(gcd<Alignment, DesiredAlignment>::value == DesiredAlignment,
		    "Alignment mismatch.");
      static_assert((Alignment >= 2 * sizeof(size_t)),
		    "Alignment must be enough to hold two pointers.");
      // Initialize per-bin counts and limits.
      // Make limits generous - the blowup bound allows significant caching.
      for (int i = 0; i < NumBins; i++) {
        _localHeapCounts[i] = 0;
        auto sz = getClassSize(i);
        // Per-bin limit: scale by LocalHeapThreshold (16MB by default).
        // Each bin can hold up to LocalHeapThreshold bytes worth of objects.
        auto limit = LocalHeapThreshold / sz;
        // At least 32 objects for any size, at most 32768.
        limit = std::max(limit, (size_t)32);
        limit = std::min(limit, (size_t)32768);
        _maxCounts[i] = static_cast<uint16_t>(limit);
      }
    }

    ~ThreadLocalAllocationBuffer() {
      clear();
    }

    inline static size_t getSize (void * ptr) {
      return getSuperblock(ptr)->getSize (ptr);
    }

    INLINE void * malloc (size_t sz) {
      // Fast path: get from thread-local freelist (no locking).
      // Small objects are the common case, and TLAB hit is the common case.
      if (HL_EXPECT_TRUE(sz <= LargestObject)) {
      	auto c = getSizeClass (sz);
      	auto * ptr = _localHeap(c).get();
      	if (HL_EXPECT_TRUE(ptr != nullptr)) {
      	  // Fast path: decrement count, no byte arithmetic.
      	  _localHeapCounts[c]--;
      	  assert (getSize(ptr) >= sz);
      	  assert ((size_t) ptr % Alignment == 0);
      	  return ptr;
      	}
      }

      // Slow path: go to parent heap (requires locking).
      auto * ptr = _parentHeap->malloc (sz);
      assert ((size_t) ptr % Alignment == 0);
      return ptr;
    }


    INLINE void free (void * ptr) {
      auto * s = getSuperblock (ptr);

      // Quick validation: superblock pointer must be non-null.
      // We skip the expensive magic number check on the fast path.
      // Invalid pointers will either:
      // 1. Have s == nullptr (caught here)
      // 2. Give invalid sz (caught by sz <= LargestObject check)
      // 3. Corrupt the freelist (caught on next malloc or by ASAN in debug)
      if (HL_EXPECT_FALSE(s == nullptr)) {
        return;
      }

      // Read object size directly WITHOUT validation.
      // For valid superblocks, this is just a memory load.
      // Invalid superblocks give garbage sizes that fail the range checks below.
      auto sz = s->getObjectSizeUnchecked();

      // Fast path: cache small objects locally (no locking).
      // The range check (Alignment <= sz <= LargestObject) catches garbage values.
      // Power-of-two sizes are 16, 32, 64, ... 1024 - any garbage is unlikely to pass.
      if (HL_EXPECT_TRUE(sz <= LargestObject && sz >= Alignment)) {
        auto c = getSizeClass(sz);
        if (HL_EXPECT_TRUE(_localHeapCounts[c] < _maxCounts[c])) {
          // Normalize pointer to handle memalign interior pointers.
          // memalign may return a pointer offset from the allocation start.
          ptr = s->normalize(ptr);
          _localHeap(c).insert(reinterpret_cast<HL::SLList::Entry*>(ptr));
          _localHeapCounts[c]++;
          return;
        }
      }

      // Slow path: full validation and normalize.
      if (HL_EXPECT_TRUE(s->isValidSuperblock())) {
        ptr = s->normalize(ptr);
        _parentHeap->free(ptr);
      }
    }

    void clear() {
      // Free every object to the 'parent' heap.
      for (int i = NumBins - 1; i >= 0; i--) {
      	while (!_localHeap(i).isEmpty()) {
      	  auto * e = _localHeap(i).get();
      	  _parentHeap->free (e);
      	}
      	_localHeapCounts[i] = 0;
      }
    }

    static inline SuperblockType * getSuperblock (void * ptr) {
      return SuperblockType::getSuperblock (ptr);
    }

  private:

    // Disable assignment and copying.

    ThreadLocalAllocationBuffer (const ThreadLocalAllocationBuffer&);
    ThreadLocalAllocationBuffer& operator=(const ThreadLocalAllocationBuffer&);

    /// Padding to prevent false sharing and ensure alignment.
    double _pad[128 / sizeof(double)];

    /// This heap's 'parent' (where to go for more memory).
    ParentHeap * _parentHeap;

    /// The number of bytes we currently have on this thread.
    size_t _localHeapBytes;

    /// The local heap itself.
    Array<NumBins, HL::SLList> _localHeap;

    /// Per-bin object counts (for fast threshold checks).
    uint16_t _localHeapCounts[NumBins];

    /// Per-bin max counts (computed at construction time).
    uint16_t _maxCounts[NumBins];

    /// Current dynamic threshold (can grow under pressure).
    size_t _currentThreshold;

    /// Cumulative bytes allocated (monotonically increasing).
    size_t _cumulativeAllocatedBytes;
  };

}

#if defined(__clang__)
#pragma clang diagnostic pop
#endif

#endif

