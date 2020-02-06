// -*- C++ -*-

/*

  The Hoard Multiprocessor Memory Allocator
  www.hoard.org

  Author: Emery Berger, http://www.emeryberger.com
  Copyright (c) 1998-2020 Emery Berger

  See the LICENSE file at the top-level directory of this
  distribution and at http://github.com/emeryberger/Hoard.

*/

#ifndef HOARD_RELEASEHEAP_H
#define HOARD_RELEASEHEAP_H

#if defined(_WIN32)
#include <windows.h>
#else
// Assume UNIX
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#endif

#include "heaplayers.h"
// #include "sassert.h"

namespace Hoard {

  template <class SuperHeap>
  class ReleaseHeap : public SuperHeap {
  public:

    enum { Alignment = SuperHeap::Alignment };

    ReleaseHeap() {
      // This heap is only safe for use when its superheap delivers
      // page-aligned memory.  Otherwise, it would run the risk of
      // releasing memory that is still in use.
      sassert<(Alignment % 4096 == 0)> ObjectsMustBePageAligned;
    }

    inline void free (void * ptr) {
      // Tell the OS it can release memory associated with this object.
      MmapWrapper::release (ptr, SuperHeap::getSize(ptr));
      // Now give it to the superheap.
      SuperHeap::free (ptr);
    }

  };

}

#endif
