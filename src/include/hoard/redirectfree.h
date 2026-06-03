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

    size_t getSize (void * ptr) {
      return Heap::getSize (ptr);
    }

    SuperblockType * getSuperblock (void * ptr) {
      return Heap::getSuperblock (ptr);
    }

    /// Free the given object, obeying the required locking protocol.
    static inline void free (void * ptr) {
      // Get the superblock header.
      SuperblockType * s = reinterpret_cast<SuperblockType *>(Heap::getSuperblock (ptr));

      assert (s->isValidSuperblock());

      // Find out who the owner is.

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
