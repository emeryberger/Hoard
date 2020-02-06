// -*- C++ -*-

/*

  The Hoard Multiprocessor Memory Allocator
  www.hoard.org

  Author: Emery Berger, http://www.emeryberger.com
  Copyright (c) 1998-2020 Emery Berger

  See the LICENSE file at the top-level directory of this
  distribution and at http://github.com/emeryberger/Hoard.

*/

#ifndef HOARD_PROCESSHEAP_H
#define HOARD_PROCESSHEAP_H

#include <cstdlib>

#include "alignedsuperblockheap.h"
#include "conformantheap.h"
#include "emptyhoardmanager.h"
#include "hoardmanager.h"
#include "hoardsuperblock.h"

namespace Hoard {

  template <size_t SuperblockSize,
	    template <class LockType_,
		      int SuperblockSize_,
		      typename HeapType_> class Header_,
	    int EmptinessClasses,
	    class LockType,
	    class ThresholdClass,
	    class MmapSource>
  class ProcessHeap :
    public ConformantHeap<
    HoardManager<AlignedSuperblockHeap<LockType, SuperblockSize, MmapSource>,
		 EmptyHoardManager<HoardSuperblock<LockType,
						   SuperblockSize,
						   ProcessHeap<SuperblockSize, Header_, EmptinessClasses, LockType, ThresholdClass, MmapSource>,
						   Header_>>,
		 HoardSuperblock<LockType,
				 SuperblockSize,
				 ProcessHeap<SuperblockSize, Header_, EmptinessClasses, LockType, ThresholdClass, MmapSource>, Header_>,
		 EmptinessClasses,
		 LockType,
		 ThresholdClass,
		 ProcessHeap<SuperblockSize, Header_, EmptinessClasses, LockType, ThresholdClass, MmapSource> > > {
  
  public:
  
    ProcessHeap (void) {}
    
    // Disable allocation from this heap.
    inline void * malloc (size_t);

  private:

    // Prevent copying or assignment.
    ProcessHeap (const ProcessHeap&);
    ProcessHeap& operator=(const ProcessHeap&);

  };

}

#endif
