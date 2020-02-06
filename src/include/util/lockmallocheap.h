// -*- C++ -*-

/*

  The Hoard Multiprocessor Memory Allocator
  www.hoard.org

  Author: Emery Berger, http://www.emeryberger.com
  Copyright (c) 1998-2020 Emery Berger

  See the LICENSE file at the top-level directory of this
  distribution and at http://github.com/emeryberger/Hoard.

*/

#ifndef HOARD_LOCKMALLOCHEAP_H
#define HOARD_LOCKMALLOCHEAP_H

// Just lock malloc (unlike LockedHeap, which locks both malloc and
// free). Meant to be combined with something like RedirectFree, which will
// implement free.

#include <mutex>

namespace Hoard {

  template <typename Heap>
    class LockMallocHeap : public Heap {
  public:
    MALLOC_FUNCTION INLINE void * malloc (size_t sz) {
      std::lock_guard<Heap> l (*this);
      return Heap::malloc (sz);
    }
  };

}

#endif
