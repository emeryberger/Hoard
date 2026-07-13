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

#include <type_traits>
#include <utility>

#include "heaplayers.h"
#include "utility/cpp23compat.h"
#include "hoard/hoardconstants.h"
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

    // Size-class dispatch. The lookup tables in sizeclasslut.h encode the
    // fine-grained classes of bins256k.h, which apply ONLY to the 256KB
    // superblock configuration (macOS/Linux). Any other configuration
    // (e.g. Windows, 64KB superblocks with generic power-of-two bins)
    // must use the bins functions supplied as template parameters:
    // indexing _localHeap with LUT classes there would run past NumBins.
    static inline int tlabSizeClass (size_t sz) {
      if constexpr (SuperblockSize == 262144) {
        return Hoard::getSizeClassLUT (sz);
      } else {
        return getSizeClass (sz);
      }
    }

    static inline size_t tlabClassSize (int c) {
      if constexpr (SuperblockSize == 262144) {
        return Hoard::getClassSizeLUT (c);
      } else {
        return getClassSize (c);
      }
    }

    ThreadLocalAllocationBuffer (ParentHeap * parent)
      : _parentHeap (parent),
        _homeHeap (nullptr),
      	_localHeapBytes (0),
        _cachedSuperblock (invalidCacheSentinel()),
        _cachedSizeClass (-1),
        _cachedClassSize (0),
        _cachedStart (0),
        _cachedObjectSize (1),
        _cachedMagicMul (0),
        _cachedMagicShift (0),
        _cachedPow2 (true),
        _cachedRemote (false),
        _myOwner (nullptr),
        _adaptiveThreshold (InitialThreshold),
        _slowPathCount (0)
    {
      static_assert(gcd<Alignment, DesiredAlignment>::value == DesiredAlignment,
		    "Alignment mismatch.");
      static_assert(SuperblockSize != 262144 || NumBins >= 20,
		    "sizeclasslut.h classes (0-19) must fit in NumBins.");
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
      	auto c = tlabSizeClass(sz);
      	auto * ptr = _localHeap(c).get();
      	if (HL_EXPECT_TRUE(ptr != nullptr)) {
          auto classSize = tlabClassSize(c);
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

      // Ultra-fast path: same superblock as last free (common in loops).
      // All fields needed to normalize the pointer and account for the
      // object are cached in this TLAB, so the superblock header is not
      // touched at all: no dependent loads through cold memory.
      if (HL_EXPECT_TRUE(s == _cachedSuperblock)) {
        ptr = normalizeCached (ptr);
        if (HL_EXPECT_TRUE(!_cachedRemote &&
                           _localHeapBytes + _cachedClassSize <= _adaptiveThreshold)) {
          _localHeap(_cachedSizeClass).insert ((HL::SLList::Entry *) ptr);
          _localHeapBytes += _cachedClassSize;
          return;
        }
        // Foreign superblock or TLAB at capacity: return the object to
        // its owning heap (RedirectFree's lock-free delayed-free path).
        // The cache stays valid: it still normalizes correctly and
        // routes subsequent frees to this same branch.
        _parentHeap->free (ptr);
        return;
      }

      // Fast path: valid superblock with small object that fits in TLAB.
      // Validity is checked exactly once here; the accessors below are
      // the unchecked variants so the magic number is not re-read.
      if (HL_EXPECT_TRUE(s != nullptr && s->isValidSuperblock())) {

      	ptr = s->normalize (ptr);
      	auto sz = s->getObjectSizeUnchecked ();

      	if (HL_EXPECT_TRUE((sz <= LargestObject) &&
                           (sz + _localHeapBytes <= _adaptiveThreshold))) {
      	  assert (getSize(ptr) >= sizeof(HL::SLList::Entry *));
          // Use lookup table for size class (faster than bsr instruction).
      	  auto c = tlabSizeClass(sz);
          auto classSize = tlabClassSize(c);

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
          // Objects from superblocks owned by another heap are NOT
          // adopted into the local bins: recycling them here would hand
          // this thread memory interleaved with other threads' live
          // objects at cache-line granularity (e.g. a shared-then-
          // scattered working set like larson's shuffled warmup), and
          // that false sharing then persists forever because a LIFO bin
          // returns the same object right back. Sending foreign objects
          // home makes the next malloc draw from this thread's own
          // superblocks, so mixed working sets migrate apart instead.
          _cachedRemote = (reinterpret_cast<const void *>(s->getOwner())
                           != _myOwner);

          if (HL_EXPECT_TRUE(!_cachedRemote)) {
            _localHeap(c).insert ((HL::SLList::Entry *) ptr);
            _localHeapBytes += classSize;
            return;
          }
          _parentHeap->free (ptr);
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

      _homeHeap = nullptr;
      _myOwner = nullptr;
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
      const size_t classSize = tlabClassSize (c);
      // Batch at most 1/8 of the current TLAB threshold per refill.
      size_t batch = _adaptiveThreshold / (8 * classSize);
      if (batch > (size_t) MaxRefillBatch) {
        batch = MaxRefillBatch;
      }
      if (batch == 0) {
        batch = 1;
      }
      // Refill from this thread's home heap: a stable heap identity for
      // the thread's whole lifetime (see assignHomeHeap). Assigned
      // lazily so early allocations before the pool exists stay cheap.
      if (HL_EXPECT_FALSE(_homeHeap == nullptr)) {
        _homeHeap = &_parentHeap->assignHomeHeap();
      }
      void * objs[MaxRefillBatch];
      size_t got = _homeHeap->mallocMany (classSize, objs, batch);
      if (HL_EXPECT_FALSE(got == 0)) {
        return nullptr;
      }
      // Remember which heap feeds this TLAB: the free path treats
      // objects from superblocks owned by any other heap as foreign
      // and sends them home rather than adopting them locally. Read
      // without the heap lock — a stale value only misroutes a free
      // (both routes are correct), never breaks anything.
      _myOwner = reinterpret_cast<const void *>
        (getSuperblock (objs[0])->getOwner());
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

    /// Padding to prevent cross-thread false sharing between TLABs.
    double _pad[Hoard::DESTRUCTIVE_INTERFERENCE_SIZE / sizeof(double)];

    /// This heap's 'parent' (where to go for more memory).
    ParentHeap * _parentHeap;

    /// The type of the per-thread heap inside the parent's thread pool.
    using HomeHeapType =
      std::remove_reference_t<decltype(std::declval<ParentHeap&>().getHeap())>;

    /// This thread's home heap: the stable refill source backing the
    /// local/foreign classification in free(). Assigned on first refill.
    HomeHeapType * _homeHeap;

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

    /// Whether the cached superblock is owned by a heap other than the
    /// one this TLAB refills from (see free()).
    bool _cachedRemote;

    /// Identity of the heap this TLAB last refilled from, as the opaque
    /// owner pointer superblock headers carry. Compared against
    /// superblock owners to classify frees as local or foreign.
    const void * _myOwner;

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
