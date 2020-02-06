// -*- C++ -*-

/*

  The Hoard Multiprocessor Memory Allocator
  www.hoard.org

  Author: Emery Berger, http://www.emeryberger.com
  Copyright (c) 1998-2020 Emery Berger

  See the LICENSE file at the top-level directory of this
  distribution and at http://github.com/emeryberger/Hoard.

*/

#ifndef HOARD_GLOBALHEAP_H
#define HOARD_GLOBALHEAP_H

#include "hoardsuperblock.h"
#include "processheap.h"

namespace Hoard {

  template <size_t SuperblockSize,
	    template <class LockType_,
		      int SuperblockSize_,
		      typename HeapType_> class Header_,
	    int EmptinessClasses,
	    class MmapSource,
	    class LockType>
  class GlobalHeap {
  
    class bogusThresholdFunctionClass {
    public:
      static inline bool function (unsigned int, unsigned int, size_t) {
	// We *never* cross the threshold for the global heap, since
	// it is the "top."
	return false;
      }
    };
  
  public:

    GlobalHeap() 
      : _theHeap (getHeap())
    {
    }
  
    typedef ProcessHeap<SuperblockSize, Header_, EmptinessClasses, LockType, bogusThresholdFunctionClass, MmapSource> SuperHeap;
    typedef HoardSuperblock<LockType, SuperblockSize, GlobalHeap, Header_> SuperblockType;
  
    void put (void * s, size_t sz) {
      assert (s);
      assert (((SuperblockType *) s)->isValidSuperblock());
      _theHeap->put ((typename SuperHeap::SuperblockType *) s,
		     sz);
    }

    SuperblockType * get (size_t sz, void * dest) {
      auto * s = 
	reinterpret_cast<SuperblockType *>
	(_theHeap->get (sz, reinterpret_cast<SuperHeap *>(dest)));
      if (s) {
	assert (s->isValidSuperblock());
      }
      return s;
    }

  private:

    SuperHeap * _theHeap;

    inline static SuperHeap * getHeap (void) {
      static double theHeapBuf[sizeof(SuperHeap) / sizeof(double) + 1];
      static auto * theHeap = new (&theHeapBuf[0]) SuperHeap;
      return theHeap;
    }

    // Prevent copying.
    GlobalHeap (const GlobalHeap&);
    GlobalHeap& operator=(const GlobalHeap&);

  };

}

#endif
