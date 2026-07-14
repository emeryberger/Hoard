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

    /// Batch allocation (used by the TLAB to amortize the heap lock).
    inline size_t mallocMany (size_t sz, void ** out, size_t n) {
      return _theHeap.mallocMany (sz, out, n);
    }

    /// Batch free: the counterpart of mallocMany, used by the TLAB to
    /// amortize the heap lock when it overflows its threshold.
    ///
    /// free() below costs TWO lock acquisitions per object (the superblock,
    /// then its owning heap) once the lock-free delayed queue is full. A
    /// thread whose live set outgrows the TLAB pays that on EVERY free --
    /// which is the whole cost of bulk workloads (a 10M-object live set ran
    /// 2.7x slower than mimalloc, all of it here). Refill has been batched
    /// since the mallocMany work above; the return path never was.
    ///
    /// Objects in a TLAB bin are all home-owned when inserted, so in the
    /// common case the whole batch belongs to one heap and one lock covers
    /// it. Ownership can still change while an object sits in the bin (its
    /// superblock can be moved to the global heap), so this does not assume
    /// it: it re-reads the owner under the lock, exactly as free() does, and
    /// only frees objects that still belong to the heap it holds. Holding an
    /// owner's lock is what prevents its superblocks from being moved out
    /// from under us (HoardManager::get/put take that same lock).
    static inline void freeMany (void ** objs, size_t n) {
      typedef BaseHoardManager<SuperblockType> * baseHeapType;

      size_t i = 0;
      while (i < n) {
        auto * s =
          reinterpret_cast<SuperblockType *>(Heap::getSuperblock (objs[i]));
        assert (s->isValidSuperblock());

        auto owner = reinterpret_cast<baseHeapType>(s->getOwner());
        assert (owner != nullptr);

        owner->lock();
        if (reinterpret_cast<baseHeapType>(s->getOwner()) != owner) {
          // Ownership changed as we took the lock. Don't spin here: hand
          // this one object to the single-object protocol, which pins the
          // superblock and is guaranteed to terminate.
          owner->unlock();
          free (objs[i]);
          i++;
          continue;
        }

        // Free this object and every following one still owned by the same
        // heap. They cannot move while we hold that heap's lock.
        do {
          owner->free (objs[i]);
          i++;
          if (i >= n) {
            break;
          }
          auto * next =
            reinterpret_cast<SuperblockType *>(Heap::getSuperblock (objs[i]));
          if (reinterpret_cast<baseHeapType>(next->getOwner()) != owner) {
            break;   // different owner: re-lock on the next iteration
          }
        } while (true);

        owner->unlock();
      }
    }

    size_t getSize (void * ptr) {
      return Heap::getSize (ptr);
    }

    SuperblockType * getSuperblock (void * ptr) {
      return Heap::getSuperblock (ptr);
    }

    /// Free the given object, obeying the required locking protocol.
    /// Fast path: try lock-free delayed free queue first.
    /// Slow path: fall back to locking protocol if queue is full.
    static inline void free (void * ptr) {
      // Get the superblock header.
      SuperblockType * s = reinterpret_cast<SuperblockType *>(Heap::getSuperblock (ptr));

      assert (s->isValidSuperblock());

      // Fast path: try lock-free delayed free.
      // The owner thread will drain these during its next malloc.
      // Bounded to 1/4 of superblock capacity to preserve blowup bounds.
      if (s->tryPushDelayedFree(ptr)) {
        return;
      }

      // Slow path: queue full, use locking protocol.
      // This is rare - only when many cross-thread frees happen faster
      // than the owner thread can drain them.

      typedef BaseHoardManager<SuperblockType> * baseHeapType;
      baseHeapType owner;

      s->lock();

      // By acquiring the lock on the superblock (above),
      // we prevent it from moving up to a higher heap.
      // This eventually pins it down in one heap,
      // so this loop is guaranteed to terminate.
      // (It should generally take no more than two iterations.)

      for (;;) {
	owner = reinterpret_cast<baseHeapType>(s->getOwner());
	assert (owner != nullptr);
	assert (owner->isValid());
	// Lock the owner. If ownership changed between these two lines,
	// we'll detect it and try again.
	owner->lock();
	if (owner == reinterpret_cast<baseHeapType>(s->getOwner())) {
	  owner->free (ptr);
	  owner->unlock();
	  s->unlock();
	  return;
	}
	owner->unlock();

	// Sleep a little.
	HL::Fred::yield();
      }
    }

  private:

    Heap _theHeap;

  };

}

#endif
