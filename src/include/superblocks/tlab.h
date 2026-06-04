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

  public:

    enum { Alignment = ParentHeap::Alignment };

    ThreadLocalAllocationBuffer (ParentHeap * parent)
      : _parentHeap (parent),
      	_localHeapBytes (0),
        _cachedSuperblock (nullptr),
        _cachedSizeClass (-1)
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

    __attribute__((always_inline)) inline void * malloc (size_t sz) {
      // Fast path: small object allocation from thread-local cache.
      // This is the common case - most allocations are small and hit the TLAB.
      if (HL_EXPECT_TRUE(sz <= LargestObject)) {
      	auto c = getSizeClass (sz);
      	auto * ptr = _localHeap(c).get();
      	if (HL_EXPECT_TRUE(ptr != nullptr)) {
      	  assert (_localHeapBytes >= getClassSize(c));
      	  _localHeapBytes -= getClassSize(c);
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


    __attribute__((always_inline)) inline void free (void * ptr) {
      auto * s = getSuperblock (ptr);

      // Ultra-fast path: same superblock as last free (common in loops).
      // Avoids isValidSuperblock() check and getObjectSize() memory access.
      if (HL_EXPECT_TRUE(s == _cachedSuperblock)) {
        ptr = s->normalize (ptr);
        if (HL_EXPECT_TRUE(_localHeapBytes + getClassSize(_cachedSizeClass) <= LocalHeapThreshold)) {
          _localHeap(_cachedSizeClass).insert ((HL::SLList::Entry *) ptr);
          _localHeapBytes += getClassSize(_cachedSizeClass);
          return;
        }
      }

      // Fast path: valid superblock with small object that fits in TLAB.
      if (HL_EXPECT_TRUE(s != nullptr && s->isValidSuperblock())) {

      	ptr = s->normalize (ptr);
      	auto sz = s->getObjectSize ();

      	if (HL_EXPECT_TRUE((sz <= LargestObject) && (sz + _localHeapBytes <= LocalHeapThreshold))) {
      	  assert (getSize(ptr) >= sizeof(HL::SLList::Entry *));
      	  auto c = getSizeClass (sz);

          // Cache this superblock for subsequent frees.
          _cachedSuperblock = s;
          _cachedSizeClass = c;

      	  _localHeap(c).insert ((HL::SLList::Entry *) ptr);
      	  _localHeapBytes += getClassSize(c);
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

  };

}

#if defined(__clang__)
#pragma clang diagnostic pop
#endif

#endif

