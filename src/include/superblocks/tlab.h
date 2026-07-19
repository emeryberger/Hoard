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

    /// One direct-mapped cache slot: a superblock plus copies of the
    /// read-only header fields the free fast path needs, so a cache hit
    /// never dereferences the (cold) superblock header. Defined up here so
    /// applyFree()'s signature (below) can name it.
    struct SbCacheEntry {
      SuperblockType * sb;       ///< owning superblock (sentinel == empty)
      uintptr_t        start;
      size_t           objectSize;
      size_t           classSize;
      size_t           magicMul;
      unsigned         magicShift;
      int              sizeClass;
      bool             pow2;
      bool             remote;   ///< owned by a heap other than our home heap
      // Pad to a power of two so indexing _sbCache is a shift, not a multiply
      // (keeps the single-superblock hit path as short as the old single
      // entry). NOT alignas: over-aligning a type that ends up inside a
      // placement-new'd heap singleton miscompiles on GCC/x86 (see CLAUDE.md).
      char             _pad[14];
    };
    static_assert(sizeof(SbCacheEntry) == 64,
                  "SbCacheEntry must stay a 64-byte power-of-two for shift indexing");

    /// Number of direct-mapped cache ways (power of two). The single-entry
    /// predecessor thrashed whenever consecutive frees hit different
    /// superblocks (larson with sizes spanning several classes): every free
    /// reloaded ~7 cold header fields and recomputed the modulo. Indexing a
    /// small set by superblock number keeps the O(1) single-compare hit path
    /// while covering the bounded working set of a scattered free stream.
    enum { SbCacheWays = 16 };

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
        _myOwner (nullptr),
        _adaptiveThreshold (InitialThreshold),
        _slowPathCount (0)
    {
      // Mark every cache slot empty. Only the sb field needs initializing:
      // the other fields are read only after sb matches, i.e. after a full
      // populate has written them.
      for (unsigned i = 0; i < SbCacheWays; i++) {
        _sbCache[i].sb = invalidCacheSentinel();
      }
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


    /// Account for a free whose superblock header fields are already cached
    /// in `e`, without touching the (cold) superblock header. Always inlined
    /// so every caller keeps the identical hot-path codegen.
    TLAB_ALWAYS_INLINE void applyFree (const SbCacheEntry & e, void * ptr) {
      ptr = normalizeWith (e, ptr);
      // ONE combined, predicted branch on the hot path. Splitting the remote
      // test from the threshold test to make room for the flush below cost
      // ~5% on 8-thread larson: every free paid the extra branch, even though
      // a workload whose live set fits in the TLAB never overflows at all.
      if (HL_EXPECT_TRUE(!e.remote &&
                         _localHeapBytes + e.classSize <= _adaptiveThreshold)) {
        _localHeap(e.sizeClass).insert ((HL::SLList::Entry *) ptr);
        _localHeapBytes += e.classSize;
        return;
      }
      // TLAB full (rare): flush a batch home under one lock. Out of line --
      // inlining it here lengthens the hot path for no benefit.
      if (HL_EXPECT_FALSE(!e.remote)) {
        freeOverflow (ptr, e.sizeClass, e.classSize);
        return;
      }
      // Foreign superblock: send it home. Kept off the helper for a reason:
      // cross-thread frees are hot at high thread counts (larson at 8 threads
      // is mostly these), and routing them through an extra call cost ~3%.
      // Objects from a foreign-owned superblock are NOT adopted into the local
      // bins -- recycling them here would hand this thread memory interleaved
      // with other threads' live objects at cache-line granularity (larson's
      // shuffled warmup), and a LIFO bin hands the same object back, so the
      // false sharing would persist forever. Sending them home lets mixed
      // working sets migrate apart instead.
      _parentHeap->free (ptr);
    }

    TLAB_ALWAYS_INLINE void free (void * ptr) {
      auto * s = getSuperblock (ptr);

      // Direct-mapped cache indexed by superblock number. A hit needs one
      // indexed load + compare and never touches the (cold) superblock
      // header. Unlike the single-entry predecessor this stays hot across a
      // scattered free stream (consecutive frees to different superblocks,
      // e.g. larson with sizes spanning several classes): the whole bounded
      // working set of superblocks lives in the set at once, so those frees
      // hit instead of reloading ~7 cold header fields and recomputing the
      // modulo every time. The compare is predicted-taken and, for both a
      // single-superblock recycle loop and a bounded scattered stream, is
      // actually taken -- so there is no mispredict penalty on the hot path.
      SbCacheEntry & e = _sbCache[sbCacheIndex (s)];
      if (HL_EXPECT_TRUE(s == e.sb)) {
        applyFree (e, ptr);
        return;
      }

      // Miss: read the header once and (re)populate this slot. Whatever
      // superblock previously occupied it is simply overwritten -- the set
      // keeps only the most recent occupant per index. Validity is checked
      // exactly once here; the accessors below are the unchecked variants so
      // the magic number is not re-read.
      if (HL_EXPECT_TRUE(s != nullptr && s->isValidSuperblock())) {
      	auto sz = s->getObjectSizeUnchecked ();
      	if (HL_EXPECT_TRUE(sz <= LargestObject)) {
          // Use lookup table for size class (faster than bsr instruction).
      	  auto c = tlabSizeClass(sz);
          e.sb = s;
          e.sizeClass = c;
          e.classSize = tlabClassSize(c);
          e.start = (uintptr_t) s->getStart();
          e.objectSize = sz;
          e.pow2 = s->objectSizeIsPowerOfTwo();
          e.magicMul = s->getMagicMul();
          e.magicShift = s->getMagicShift();
          e.remote = (reinterpret_cast<const void *>(s->getOwner())
                      != _myOwner);
          applyFree (e, ptr);
          return;
      	}
      	// Large object - free to parent heap. Not cached (its size class is
        // outside the small-object range these slots describe), so no cache
        // entry can ever alias it; leave the cache untouched.
      	_parentHeap->free (s->normalize (ptr));
      }
    }

    void clear() {
      // Invalidate every superblock cache slot.
      for (unsigned i = 0; i < SbCacheWays; i++) {
        _sbCache[i].sb = invalidCacheSentinel();
      }

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

    /// Map a superblock to its cache slot. Superblocks are SuperblockSize-
    /// aligned, so their "number" (address / SuperblockSize) is dense and
    /// consecutive superblocks land in distinct slots.
    static inline unsigned sbCacheIndex (SuperblockType * s) {
      return (unsigned) (((uintptr_t) s / SuperblockSize) & (SbCacheWays - 1));
    }

    /// Normalize a pointer using cached header fields only (no header access).
    TLAB_ALWAYS_INLINE void * normalizeWith (const SbCacheEntry & e,
                                             void * ptr) const {
      uintptr_t offset = (uintptr_t) ptr - e.start;
      size_t remainder;
      if (HL_EXPECT_TRUE(e.pow2)) {
        remainder = offset & (e.objectSize - 1);
      } else {
#if defined(_MSC_VER) && !defined(__clang__)
        remainder = offset % e.objectSize;
#else
        size_t quotient =
          (size_t)(((__uint128_t) offset * e.magicMul) >> e.magicShift);
        remainder = offset - quotient * e.objectSize;
#endif
      }
      return (void *) ((uintptr_t) ptr - remainder);
    }

    /// Maximum number of objects fetched per bin refill.
    enum { MaxRefillBatch = 64 };

    /// Maximum number of objects returned home per bin flush. The mirror
    /// image of MaxRefillBatch.
    enum { MaxFlushBatch = 64 };

    /// Flush up to MaxFlushBatch objects from bin c back to their owning
    /// heap under a SINGLE lock acquisition; return how many were flushed
    /// (0 if the bin was empty).
    ///
    /// This is the counterpart of refill(). Without it, a thread whose live
    /// set outgrows the TLAB pays two lock acquisitions (superblock, then
    /// owning heap) on EVERY free, because each object is handed to the
    /// parent heap one at a time -- the dominant cost of any bulk workload.
    ///
    /// Blowup preservation: flushing only ever REMOVES objects from the
    /// TLAB, so _localHeapBytes <= _adaptiveThreshold <= LocalHeapThreshold
    /// still holds. The caller flushes before inserting, and each flush frees
    /// at least one object of the class it is about to insert, so the
    /// threshold is never exceeded.
    /// Cold path of free(): the object is either foreign, or the TLAB is at
    /// capacity. Deliberately NOT inlined into free(): it runs rarely (never
    /// at all for a workload whose live set fits in the TLAB), and inlining
    /// it lengthened the hot path enough to cost ~3% on 8-thread larson.
    NO_INLINE void freeOverflow (void * ptr, int c, size_t classSize) {
      // Only reached when the object is home-owned and the TLAB is at
      // capacity. Make room by flushing a BATCH home under one lock, rather
      // than paying two lock acquisitions for this single object.
      if (HL_EXPECT_TRUE(flushBin (c, classSize) != 0)) {
        _localHeap(c).insert (reinterpret_cast<HL::SLList::Entry *>(ptr));
        _localHeapBytes += classSize;
        return;
      }
      // Nothing in this bin to flush (the TLAB is full of other classes):
      // send this object to its owning heap, as before.
      _parentHeap->free (ptr);
    }

    NO_INLINE size_t flushBin (int c, size_t classSize) {
      void * objs[MaxFlushBatch];
      size_t n = 0;
      while (n < (size_t) MaxFlushBatch) {
        auto * e = _localHeap(c).get();
        if (e == nullptr) {
          break;
        }
        objs[n++] = e;
      }
      if (n == 0) {
        return 0;
      }
      _localHeapBytes -= n * classSize;
      if (HL_EXPECT_TRUE(_homeHeap != nullptr)) {
        // Batch: one lock for the whole run of same-owner objects.
        _homeHeap->freeMany (objs, n);
      } else {
        // No home heap yet (nothing has refilled): objects can only have
        // reached a bin via a home-owned free, but stay correct regardless.
        for (size_t i = 0; i < n; i++) {
          _parentHeap->free (objs[i]);
        }
      }
      return n;
    }

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

    /// Direct-mapped cache of recently-freed superblocks' header fields,
    /// indexed by sbCacheIndex(). See SbCacheEntry / free().
    SbCacheEntry _sbCache[SbCacheWays];

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
