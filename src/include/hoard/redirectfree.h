// -*- C++ -*-

/*

  The Hoard Multiprocessor Memory Allocator
  www.hoard.org

  Author: Emery Berger, http://www.emeryberger.com
  Copyright (c) 1998-2020 Emery Berger

  See the LICENSE file at the top-level directory of this
  distribution and at http://github.com/emeryberger/Hoard.

*/

#ifndef HOARD_REDIRECTFREE_H
#define HOARD_REDIRECTFREE_H

#include "heaplayers.h"

// Branch prediction hints (mimalloc-style optimization)
#if defined(__GNUC__) || defined(__clang__)
#define RF_LIKELY(x) __builtin_expect(!!(x), 1)
#define RF_UNLIKELY(x) __builtin_expect(!!(x), 0)
#else
#define RF_LIKELY(x) (x)
#define RF_UNLIKELY(x) (x)
#endif

namespace Hoard {

  /**
   * @class RedirectFree
   * @brief Routes free calls to the Superblock's owner heap.
   * @note  We also lock the heap on calls to malloc.
   */

  template <class Heap,
	    typename SuperblockType_>
  class RedirectFree {
  public:

    enum { Alignment = Heap::Alignment };

    typedef SuperblockType_ SuperblockType;

    RedirectFree()
    {
    }

    inline void * malloc (size_t sz) {
      void * ptr = _theHeap.malloc (sz);
      assert (getSize(ptr) >= sz);
      assert ((size_t) ptr % Alignment == 0);
      return ptr;
    }

    size_t getSize (void * ptr) {
      return Heap::getSize (ptr);
    }

    SuperblockType * getSuperblock (void * ptr) {
      return Heap::getSuperblock (ptr);
    }

    /// Drain all delayed frees (passthrough to underlying heap).
    /// Called on thread exit to ensure pending frees are processed.
    void drainAllDelayedFrees() {
      _theHeap.drainAllDelayedFrees();
    }

    /// Check if this heap is active (passthrough to underlying heap).
    bool isActive() const {
      return _theHeap.isActive();
    }

    /// Mark this heap as active or inactive (passthrough to underlying heap).
    void setActive(bool active) {
      _theHeap.setActive(active);
    }

    /// Free the given object using delayed queue for cross-thread frees.
    ///
    /// This implements mimalloc-style delayed frees:
    /// - Push to lock-free queue (single atomic CAS, no locks)
    /// - Owner thread drains queue during malloc
    /// - When heap becomes active again, pending frees are drained
    ///
    /// Note: This is the slow path. Fast path for small objects is in TLAB.
    inline void free (void * ptr) {
      // Get the superblock header via bitmask (fast O(1) lookup).
      SuperblockType * s = reinterpret_cast<SuperblockType *>(Heap::getSuperblock (ptr));

      if (RF_UNLIKELY(!s || !s->isValidSuperblock())) {
        return; // Invalid pointer, ignore
      }

      // Normalize pointer to handle interior pointers.
      ptr = s->normalize(ptr);

      // Push to delayed free queue (lock-free!)
      // The owner thread will drain this queue during its next malloc.
      // If owner is inactive, frees accumulate until heap is reactivated.
      s->pushDelayedFree(ptr);
    }

  private:

    Heap _theHeap;

  };

}

#endif
