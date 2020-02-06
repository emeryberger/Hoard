// -*- C++ -*-

/*

  The Hoard Multiprocessor Memory Allocator
  www.hoard.org

  Author: Emery Berger, http://www.emeryberger.com
  Copyright (c) 1998-2020 Emery Berger

  See the LICENSE file at the top-level directory of this
  distribution and at http://github.com/emeryberger/Hoard.

*/

#ifndef HOARD_IGNOREINVALIDFREE_H
#define HOARD_IGNOREINVALIDFREE_H

namespace Hoard {

  // A class that checks to see if the object to be freed is inside a
  // valid superblock. If not, it drops the object on the floor. We do
  // this in the name of robustness (turning a segfault or data
  // corruption into a potential memory leak) and because on some
  // systems, it's impossible to catch the first few allocated objects.

  template <class SuperHeap>
  class IgnoreInvalidFree : public SuperHeap {
  public:
    INLINE void free (void * ptr) {
      if (ptr) {
	typename SuperHeap::SuperblockType * s = SuperHeap::getSuperblock (ptr);
	if (!s || (!s->isValidSuperblock())) {
	  // We encountered an invalid free, so we drop it.
	  return;
	}
	SuperHeap::free (ptr);
      }
    }

    INLINE size_t getSize (void * ptr) {
      if (ptr) {
	typename SuperHeap::SuperblockType * s = SuperHeap::getSuperblock (ptr);
	if (!s || (!s->isValidSuperblock())) {
	  return 0;
	}
	return SuperHeap::getSize (ptr);
      } else {
	return 0;
      }
    }

  };

}

#endif
