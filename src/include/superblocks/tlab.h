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
#include "hoard/sizeclasslut.h"

#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-variable"
#endif

// Cross-platform force inline
#if defined(_MSC_VER)
#define TLAB_ALWAYS_INLINE __forceinline
#elif defined(__GNUC__) || defined(__clang__)
#define TLAB_ALWAYS_INLINE __attribute__((always_inline)) inline
#else
#define TLAB_ALWAYS_INLINE inline
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

    // Adaptive TLAB sizing:
    // The TLAB threshold adapts based on allocation patterns while preserving
    // blowup bounds. We track peak TLAB usage and allow growth up to a fraction
    // of LocalHeapThreshold. This reduces memory for low-allocation threads
    // while allowing high-throughput threads to grow their cache.
    //
    // Blowup preservation: _adaptiveThreshold ≤ LocalHeapThreshold always,
    // so total TLAB memory never exceeds the original O(1) bound.
    enum { InitialThreshold = 65536 };           // 64KB initial
    enum { MinThreshold = 16384 };               // 16KB minimum
    enum { GrowthFactor = 2 };                   // Double when TLAB fills
    enum { SlowPathGrowTrigger = 16 };           // Grow after this many slow paths

  public:

    enum { Alignment = ParentHeap::Alignment };

    ThreadLocalAllocationBuffer (ParentHeap * parent)
      : _parentHeap (parent),
      	_localHeapBytes (0),
        _cachedSuperblock (nullptr),
        _cachedSizeClass (-1),
        _adaptiveThreshold (InitialThreshold),
        _slowPathCount (0)
    {
      static_assert(gcd<Alignment, DesiredAlignment>::value == DesiredAlignment,
		    "Alignment mismatch.");
      static_assert((Alignment >= 2 * sizeof(size_t)),
		    "Alignment must be enough to hold two pointers.");
    }

    ~ThreadLocalAllocationBuffer() {
      clear();
    }

    inline static size_t getSize (void * ptr) {
      return getSuperblock(ptr)->getSize (ptr);
    }

    TLAB_ALWAYS_INLINE void * malloc (size_t sz) {
      // Fast path: small object allocation from thread-local cache.
      // This is the common case - most allocations are small and hit the TLAB.
      if (HL_EXPECT_TRUE(sz <= LargestObject)) {
        // Use lookup table for size class (faster than bsr instruction).
        // LUT handles sizes 1-1024, which covers LargestSmallObject.
      	auto c = Hoard::getSizeClassLUT(sz);
      	auto * ptr = _localHeap(c).get();
      	if (HL_EXPECT_TRUE(ptr != nullptr)) {
          auto classSize = Hoard::getClassSizeLUT(c);
      	  assert (_localHeapBytes >= classSize);
      	  _localHeapBytes -= classSize;
      	  assert (getSize(ptr) >= sz);
      	  assert ((size_t) ptr % Alignment == 0);
      	  return ptr;
      	}
      }

      // Slow path: go to parent heap (requires locking).
      // Adapt threshold: grow TLAB if we're hitting slow path often.
      maybeGrowThreshold();

      auto * ptr = _parentHeap->malloc (sz);
      assert ((size_t) ptr % Alignment == 0);
      return ptr;
    }


    TLAB_ALWAYS_INLINE void free (void * ptr) {
      auto * s = getSuperblock (ptr);

      // Use adaptive threshold (bounded by LocalHeapThreshold for blowup guarantee).
      const size_t threshold = _adaptiveThreshold;

      // Ultra-fast path: same superblock as last free (common in loops).
      // Avoids isValidSuperblock() check and getObjectSize() memory access.
      if (HL_EXPECT_TRUE(s == _cachedSuperblock)) {
        ptr = s->normalize (ptr);
        auto classSize = Hoard::getClassSizeLUT(_cachedSizeClass);
        if (HL_EXPECT_TRUE(_localHeapBytes + classSize <= threshold)) {
          _localHeap(_cachedSizeClass).insert ((HL::SLList::Entry *) ptr);
          _localHeapBytes += classSize;
          return;
        }
      }

      // Fast path: valid superblock with small object that fits in TLAB.
      if (HL_EXPECT_TRUE(s != nullptr && s->isValidSuperblock())) {

      	ptr = s->normalize (ptr);
      	auto sz = s->getObjectSize ();

      	if (HL_EXPECT_TRUE((sz <= LargestObject) && (sz + _localHeapBytes <= threshold))) {
      	  assert (getSize(ptr) >= sizeof(HL::SLList::Entry *));
          // Use lookup table for size class (faster than bsr instruction).
      	  auto c = Hoard::getSizeClassLUT(sz);
          auto classSize = Hoard::getClassSizeLUT(c);

          // Cache this superblock for subsequent frees.
          _cachedSuperblock = s;
          _cachedSizeClass = c;

      	  _localHeap(c).insert ((HL::SLList::Entry *) ptr);
      	  _localHeapBytes += classSize;
      	  return;
      	}

      	// Slow path: large object or TLAB full - free to parent heap.
        _cachedSuperblock = nullptr;  // Invalidate cache
      	_parentHeap->free (ptr);
      }
    }

    void clear() {
      // Invalidate the superblock cache.
      _cachedSuperblock = nullptr;
      _cachedSizeClass = -1;

      // Free every object to the 'parent' heap.
      int i = NumBins - 1;
      while ((_localHeapBytes > 0) && (i >= 0)) {
      	auto sz = getClassSize (i);
      	while (!_localHeap(i).isEmpty()) {
      	  auto * e = _localHeap(i).get();
      	  _parentHeap->free (e);
      	  _localHeapBytes -= sz;
      	}
      	i--;
      }
    }

    static inline SuperblockType * getSuperblock (void * ptr) {
      return SuperblockType::getSuperblock (ptr);
    }

  private:

    // Disable assignment and copying.

    ThreadLocalAllocationBuffer (const ThreadLocalAllocationBuffer&);
    ThreadLocalAllocationBuffer& operator=(const ThreadLocalAllocationBuffer&);

    /// Grow TLAB threshold when hitting slow path often.
    /// Preserves blowup by never exceeding LocalHeapThreshold.
    inline void maybeGrowThreshold() {
      _slowPathCount++;
      if (_slowPathCount >= SlowPathGrowTrigger && _adaptiveThreshold < LocalHeapThreshold) {
        // Double the threshold, capped at LocalHeapThreshold.
        size_t newThreshold = _adaptiveThreshold * GrowthFactor;
        _adaptiveThreshold = (newThreshold < LocalHeapThreshold) ? newThreshold : LocalHeapThreshold;
        _slowPathCount = 0;
      }
    }

    /// Padding to prevent false sharing and ensure alignment.
    double _pad[128 / sizeof(double)];

    /// This heap's 'parent' (where to go for more memory).
    ParentHeap * _parentHeap;

    /// The number of bytes we currently have on this thread.
    size_t _localHeapBytes;

    /// Cached superblock pointer for fast consecutive frees.
    SuperblockType * _cachedSuperblock;

    /// Cached size class for the cached superblock.
    int _cachedSizeClass;

    /// The local heap itself.
    Array<NumBins, HL::SLList> _localHeap;

    /// Adaptive TLAB threshold (starts at InitialThreshold, grows to LocalHeapThreshold).
    /// Bounded above by LocalHeapThreshold to preserve blowup guarantee.
    size_t _adaptiveThreshold;

    /// Count of slow path hits for growth heuristic.
    unsigned int _slowPathCount;

  };

}

#if defined(__clang__)
#pragma clang diagnostic pop
#endif

#endif

