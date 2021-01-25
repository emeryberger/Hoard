// -*- C++ -*-

/*

  The Hoard Multiprocessor Memory Allocator
  www.hoard.org

  Author: Emery Berger, http://www.emeryberger.com
  Copyright (c) 1998-2020 Emery Berger

  See the LICENSE file at the top-level directory of this
  distribution and at http://github.com/emeryberger/Hoard.

*/

#ifndef HOARD_ADDHEADERHEAP_H
#define HOARD_ADDHEADERHEAP_H

#include "heaplayers.h"

namespace Hoard {

  /**
   * @class AddHeaderHeap
   */

  template <class SuperblockType,
	    size_t SuperblockSize,
	    class SuperHeap>
  class AddHeaderHeap {
  private:

    static_assert(((int) SuperHeap::Alignment) % SuperblockSize == 0,
		  "Superblock size must divide evenly into Alignment.");
    static_assert(((int) SuperHeap::Alignment) >= SuperblockSize,
		  "Alignment must be at least as large as the Superblock size.");

    SuperHeap theHeap;

  public:

    enum { Alignment = gcd<SuperHeap::Alignment, sizeof(typename SuperblockType::Header)>::value };

    void clear() {
      theHeap.clear();
    }

    MALLOC_FUNCTION INLINE void * malloc (size_t sz) {

      // Allocate extra space for the header,
      // put it at the front of the object,
      // and return a pointer to just past it.
      const size_t headerSize = sizeof(typename SuperblockType::Header);
      void * ptr = theHeap.malloc (sz + headerSize);
      if (ptr == nullptr) {
	return nullptr;
      }
      typename SuperblockType::Header * p
	= new (ptr) typename SuperblockType::Header (sz, sz);
      assert ((size_t) (p + 1) == (size_t) ptr + headerSize);
      return reinterpret_cast<void *>(p + 1);
    }

    INLINE static size_t getSize (void * ptr) {
      // Find the header (just before the pointer) and return the size
      // value stored there.
      typename SuperblockType::Header * p;
      p = reinterpret_cast<typename SuperblockType::Header *>(ptr);
      return (p - 1)->getSize (ptr);
    }

    INLINE void free (void * ptr) {
      // Find the header (just before the pointer) and free the whole object.
      typename SuperblockType::Header * p;
      p = reinterpret_cast<typename SuperblockType::Header *>(ptr);
      theHeap.free (reinterpret_cast<void *>(p - 1), getSize(ptr));
    }

    INLINE void free (void * ptr, size_t sz) {
      // Find the header (just before the pointer) and free the whole object.
      typename SuperblockType::Header * p;
      p = reinterpret_cast<typename SuperblockType::Header *>(ptr);
      theHeap.free (reinterpret_cast<void *>(p - 1), sz);
    }
  };

}

#endif
