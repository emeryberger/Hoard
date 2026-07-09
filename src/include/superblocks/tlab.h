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
        _cachedSuperblock (invalidCacheSentinel()),
        _cachedSizeClass (-1),
        _cachedClassSize (0),
        _cachedStart (0),
        _cachedObjectSize (1),
        _cachedMagicMul (0),
        _cachedMagicShift (0),
        _cachedPow2 (true),
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
        // Refill the bin with a batch of objects under a single parent
        // heap lock acquisition, rather than paying a lock per object.
        void * rptr = refill (c);
        if (HL_EXPECT_TRUE(rptr != nullptr)) {
          assert ((size_t) rptr % Alignment == 0);
          return rptr;
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
      // All fields needed to normalize the pointer and account for the
      // object are cached in this TLAB, so the superblock header is not
      // touched at all: no dependent loads through cold memory.
      if (HL_EXPECT_TRUE(s == _cachedSuperblock)) {
        ptr = normalizeCached (ptr);
        if (HL_EXPECT_TRUE(_localHeapBytes + _cachedClassSize <= threshold)) {
          _localHeap(_cachedSizeClass).insert ((HL::SLList::Entry *) ptr);
          _localHeapBytes += _cachedClassSize;
          return;
        }
      }

      // Fast path: valid superblock with small object that fits in TLAB.
      // Validity is checked exactly once here; the accessors below are
      // the unchecked variants so the magic number is not re-read.
      if (HL_EXPECT_TRUE(s != nullptr && s->isValidSuperblock())) {

      	ptr = s->normalize (ptr);
      	auto sz = s->getObjectSizeUnchecked ();

      	if (HL_EXPECT_TRUE((sz <= LargestObject) && (sz + _localHeapBytes <= threshold))) {
      	  assert (getSize(ptr) >= sizeof(HL::SLList::Entry *));
          // Use lookup table for size class (faster than bsr instruction).
      	  auto c = Hoard::getSizeClassLUT(sz);
          auto classSize = Hoard::getClassSizeLUT(c);

          // Cache this superblock (and the header fields the cached
          // path needs) for subsequent frees.
          _cachedSuperblock = s;
          _cachedSizeClass = c;
          _cachedClassSize = classSize;
          _cachedStart = (uintptr_t) s->getStart();
          _cachedObjectSize = sz;
          _cachedPow2 = s->objectSizeIsPowerOfTwo();
          _cachedMagicMul = s->getMagicMul();
          _cachedMagicShift = s->getMagicShift();

      	  _localHeap(c).insert ((HL::SLList::Entry *) ptr);
      	  _localHeapBytes += classSize;
      	  return;
      	}

      	// Slow path: large object or TLAB full - free to parent heap.
        _cachedSuperblock = invalidCacheSentinel();  // Invalidate cache
      	_parentHeap->free (ptr);
      }
    }

    void clear() {
      // Invalidate the superblock cache.
      _cachedSuperblock = invalidCacheSentinel();
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

    /// Sentinel for "no cached superblock". Deliberately NOT nullptr:
    /// getSuperblock() of a garbage near-zero pointer yields nullptr,
    /// which must not match the cache (the cached path performs no
    /// validity check). The value 1 can never equal a real superblock
    /// address (those are SuperblockSize-aligned).
    static SuperblockType * invalidCacheSentinel() {
      return reinterpret_cast<SuperblockType *>(uintptr_t(1));
    }

    /// Normalize a pointer into the cached superblock using the cached
    /// header fields only (no superblock header access).
    TLAB_ALWAYS_INLINE void * normalizeCached (void * ptr) const {
      uintptr_t offset = (uintptr_t) ptr - _cachedStart;
      size_t remainder;
      if (HL_EXPECT_TRUE(_cachedPow2)) {
        remainder = offset & (_cachedObjectSize - 1);
      } else {
#if defined(_MSC_VER) && !defined(__clang__)
        remainder = offset % _cachedObjectSize;
#else
        size_t quotient =
          (size_t)(((__uint128_t) offset * _cachedMagicMul) >> _cachedMagicShift);
        remainder = offset - quotient * _cachedObjectSize;
#endif
      }
      return (void *) ((uintptr_t) ptr - remainder);
    }

    /// Maximum number of objects fetched per bin refill.
    enum { MaxRefillBatch = 64 };

    /// Refill an empty bin with a batch of objects fetched under one
    /// parent-heap lock acquisition; return one of them.
    ///
    /// Blowup preservation: the prefetched objects live in the TLAB and
    /// are bounded by the same _adaptiveThreshold ≤ LocalHeapThreshold
    /// accounting as objects cached by free(), so the O(1)-per-thread
    /// bound on TLAB memory is unchanged.
    NO_INLINE void * refill (int c) {
      maybeGrowThreshold();
      const size_t classSize = Hoard::getClassSizeLUT (c);
      // Batch at most 1/8 of the current TLAB threshold per refill.
      size_t batch = _adaptiveThreshold / (8 * classSize);
      if (batch > (size_t) MaxRefillBatch) {
        batch = MaxRefillBatch;
      }
      if (batch == 0) {
        batch = 1;
      }
      void * objs[MaxRefillBatch];
      size_t got = _parentHeap->getHeap().mallocMany (classSize, objs, batch);
      if (HL_EXPECT_FALSE(got == 0)) {
        return nullptr;
      }
      for (size_t i = 1; i < got; i++) {
        _localHeap(c).insert (reinterpret_cast<HL::SLList::Entry *>(objs[i]));
      }
      _localHeapBytes += (got - 1) * classSize;
      return objs[0];
    }

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

    // Copies of the cached superblock's read-only header fields, so the
    // cached free path never dereferences the superblock header.
    size_t _cachedClassSize;
    uintptr_t _cachedStart;
    size_t _cachedObjectSize;
    size_t _cachedMagicMul;
    unsigned _cachedMagicShift;
    bool _cachedPow2;

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

