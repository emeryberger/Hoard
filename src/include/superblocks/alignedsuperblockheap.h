// -*- C++ -*-

/*

  The Hoard Multiprocessor Memory Allocator
  www.hoard.org

  Author: Emery Berger, http://www.emeryberger.com
  Copyright (c) 1998-2020 Emery Berger

  See the LICENSE file at the top-level directory of this
  distribution and at http://github.com/emeryberger/Hoard.

*/

#ifndef HOARD_ALIGNEDSUPERBLOCKHEAP_H
#define HOARD_ALIGNEDSUPERBLOCKHEAP_H

#include "heaplayers.h"

#include "conformantheap.h"
#include "fixedrequestheap.h"

namespace Hoard {

  template <size_t SuperblockSize,
	    class TheLockType,
	    class MmapSource>
  class SuperblockStore {
  public:

    enum { Alignment = MmapSource::Alignment };

    SuperblockStore() {
#if defined(__SVR4)
      // We only get 64K chunks from mmap on Solaris, so we need to grab
      // more chunks (and align them to 64K!) for smaller superblock sizes.
      // Right now, we do not handle this case and just assert here that
      // we are getting chunks of 64K.
      static_assert(SuperblockSize == 65536,
		    "This is needed to prevent mmap fragmentation.");
#endif
    }
    
    void * malloc (size_t) {
      if (_freeSuperblocks.isEmpty()) {
	// Get more memory.
	void * ptr = _superblockSource.malloc (ChunksToGrab * SuperblockSize);
	if (!ptr) {
	  return nullptr;
	}
	char * p = (char *) ptr;
	for (int i = 0; i < ChunksToGrab; i++) {
	  _freeSuperblocks.insert ((DLList::Entry *) p);
	  p += SuperblockSize;
	}
      }
      return _freeSuperblocks.get();
    }

    void free (void * ptr) {
      _freeSuperblocks.insert ((DLList::Entry *) ptr);
    }

  private:

#if defined(__SVR4)
    enum { ChunksToGrab = 1 };
#else
    enum { ChunksToGrab = 1 };
#endif

    MmapSource _superblockSource;
    DLList _freeSuperblocks;

  };

}


namespace Hoard {

  template <class TheLockType,
	    size_t SuperblockSize,
	    class MmapSource>
  class AlignedSuperblockHeapHelper :
    public ConformantHeap<HL::LockedHeap<TheLockType,
					 FixedRequestHeap<SuperblockSize, 
							  SuperblockStore<SuperblockSize, TheLockType, MmapSource> > > > {};


#if 0

  template <class TheLockType,
	    size_t SuperblockSize>
  class AlignedSuperblockHeap : public AlignedMmap<SuperblockSize,TheLockType> {};


#else

  template <class TheLockType,
	    size_t SuperblockSize,
	    class MmapSource>
  class AlignedSuperblockHeap :
    public AlignedSuperblockHeapHelper<TheLockType, SuperblockSize, MmapSource> {
  public:
    AlignedSuperblockHeap() {
      static_assert(AlignedSuperblockHeapHelper<TheLockType, SuperblockSize, MmapSource>::Alignment % SuperblockSize == 0,
		    "Ensure proper alignment.");
    }

  };
#endif

}

#endif
