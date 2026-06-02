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
   *
   * Uses lock-free delayed free for cross-thread frees:
   * - Same-thread free: lock owner heap, free directly
   * - Cross-thread free: push to superblock's lock-free queue (no locks)
   * - On malloc: drain pending cross-thread frees from active superblocks
   */

  template <class Heap,
	    typename SuperblockType_>
  class RedirectFree {
  public:

    enum { Alignment = Heap::Alignment };

    typedef SuperblockType_ SuperblockType;

    RedirectFree() {}

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

    /// Free the given object using delayed free for cross-thread.
    static inline void free (void * ptr) {
      // Get the superblock header.
      SuperblockType * s = reinterpret_cast<SuperblockType *>(Heap::getSuperblock (ptr));
      assert (s->isValidSuperblock());

      // Check if this is our own thread's superblock.
      size_t myTid = HL::CPUInfo::getThreadId();
      size_t ownerTid = s->getOwnerTid();

      if (myTid == ownerTid) {
        // Fast path: local free.
        localFree(ptr, s);
      } else {
        // Lock-free path: push to superblock's cross-thread queue.
        s->crossThreadFree(ptr);
      }
    }

  private:

    /// Fast path: free to our own heap (single lock, no superblock lock).
    static inline void localFree(void * ptr, SuperblockType * s) {
      typedef BaseHoardManager<SuperblockType> * baseHeapType;
      baseHeapType owner = reinterpret_cast<baseHeapType>(s->getOwner());
      assert(owner != nullptr);
      assert(owner->isValid());

      owner->lock();

      // First, drain any pending cross-thread frees for this superblock.
      drainCrossThreadFrees(s, owner);

      // Now free our object.
      owner->free(ptr);
      owner->unlock();
    }

    /// Drain pending cross-thread frees from a superblock into its owner heap.
    static inline void drainCrossThreadFrees(SuperblockType * s,
                                              BaseHoardManager<SuperblockType> * owner) {
      auto* entry = s->drainCrossThreadFrees();
      while (entry != nullptr) {
        void* ptr = reinterpret_cast<void*>(entry);
        auto* next = entry->next;
        owner->free(ptr);
        entry = next;
      }
    }

    Heap _theHeap;
  };

}

#endif
