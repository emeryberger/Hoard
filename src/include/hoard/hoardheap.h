// -*- C++ -*-

/*

  The Hoard Multiprocessor Memory Allocator
  www.hoard.org

  Author: Emery Berger, http://www.cs.umass.edu/~emery
 
  Copyright (c) 1998-2012 Emery Berger
  
  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
  (at your option) any later version.
  
  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.
  
  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA

*/

#ifndef HOARD_HOARDHEAP_H
#define HOARD_HOARDHEAP_H

#include "heaplayers.h"

using namespace HL;

// The minimum allocation grain for a given object -
// that is, we carve objects out of chunks of this size.
#define SUPERBLOCK_SIZE 65536

// The number of 'emptiness classes'; see the ASPLOS paper for details.
#define EMPTINESS_CLASSES 8


// Hoard-specific Heap Layers

#include "thresholdheap.h"
#include "bins64k.h"
#include "hoardmanager.h"
#include "addheaderheap.h"
#include "threadpoolheap.h"
#include "redirectfree.h"
#include "ignoreinvalidfree.h"
#include "conformantheap.h"
#include "hoardsuperblock.h"
#include "lockmallocheap.h"
#include "alignedsuperblockheap.h"
#include "alignedmmap.h"
#include "globalheap.h"

// Generic Heap Layers

#include "ansiwrapper.h"
#include "debugheap.h"
#include "lockedheap.h"

//#include "bins4k.h"
//#include "bins8k.h"
//#include "bins16k.h"
#include "oneheap.h"
#include "freelistheap.h"
#include "hybridheap.h"

// Note: I plan to eventually eliminate the use of the spin lock,
// since the right place to do locking is in an OS-supplied library,
// and platforms have substantially improved the efficiency of these
// primitives.

#if defined(_WIN32)
#include "winlock.h"
typedef HL::WinLockType TheLockType;
#elif defined(__APPLE__)
#include "maclock.h"
// NOTE: On older versions of the Mac OS, Hoard CANNOT use Posix locks,
// since they may call malloc themselves. However, as of Snow Leopard,
// that problem seems to have gone away. Nonetheless, we use Mac-specific locks.
typedef HL::MacLockType TheLockType;
#elif defined(__SVR4)
#include "spinlock.h"
typedef HL::SpinLockType TheLockType;
#else
#include "posixlock.h"
#include "spinlock.h"
typedef HL::SpinLockType TheLockType;
// typedef HL::PosixLockType TheLockType;
#endif

#include "exactlyoneheap.h"
#include "releaseheap.h"
#include "threadheap.h"

namespace Hoard {

  class ThresholdedMmapSource :
#if 0
    public ReleaseHeap<ThresholdHeap<1048576,
				     1, EMPTINESS_CLASSES,
				     AlignedMmap<SUPERBLOCK_SIZE, TheLockType> > >
#else
    public ThresholdHeap<1048576,
			 1, EMPTINESS_CLASSES,
			 AlignedMmap<SUPERBLOCK_SIZE, TheLockType> >
#endif
  {};
  
#if 0
  class ThresholdedLevelOne :
    public 
    ThresholdHeap<1048576,
		  1, EMPTINESS_CLASSES,
		  ExactlyOneHeap<LockedHeap<TheLockType, ThresholdedMmapSource> > > {
  };

  class MmapSource :
    public ExactlyOneHeap<ThreadHeap<32, LockedHeap<TheLockType, ThresholdedLevelOne> > > {};
  
#else
  // class MmapSource : public ExactlyOneHeap<LockedHeap<TheLockType, ThresholdedMmapSource> > {};
  class MmapSource : public AlignedMmap<SUPERBLOCK_SIZE, TheLockType> {};
#endif
  
  //
  // There is just one "global" heap, shared by all of the per-process heaps.
  //

  typedef GlobalHeap<SUPERBLOCK_SIZE, EMPTINESS_CLASSES, MmapSource, TheLockType>
  TheGlobalHeap;
  
  //
  // When a thread frees memory and causes a per-process heap to fall
  // below the emptiness threshold given in the function below, it
  // moves a (nearly or completely empty) superblock to the global heap.
  //

  class hoardThresholdFunctionClass {
  public:
    inline static bool function (int u, int a, size_t objSize) {
      /*
	Returns 1 iff we've crossed the emptiness threshold:
	
	U < A - 2S   &&   U < EMPTINESS_CLASSES-1/EMPTINESS_CLASSES * A
	
      */
      bool r = ((EMPTINESS_CLASSES * u) < ((EMPTINESS_CLASSES-1) * a)) && ((u < a - (2 * SUPERBLOCK_SIZE) / (int) objSize));
      return r;
    }
  };
  

  class SmallHeap;
  
  typedef HoardSuperblock<TheLockType, SUPERBLOCK_SIZE, SmallHeap> SmallSuperblockType;

  //
  // The heap that manages small objects.
  //
  class SmallHeap : 
    public ConformantHeap<
    HoardManager<AlignedSuperblockHeap<TheLockType, SUPERBLOCK_SIZE, MmapSource>,
		 TheGlobalHeap,
		 SmallSuperblockType,
		 EMPTINESS_CLASSES,
		 TheLockType,
		 hoardThresholdFunctionClass,
		 SmallHeap> > {
  public:
    void * malloc (size_t sz) {
      void * ptr = SuperHeap::malloc (sz);
      assert (getSize(ptr) >= sz);
      assert ((size_t) ptr % Alignment == 0);
      return ptr;
    }
  };

  class BigHeap;

  typedef HoardSuperblock<TheLockType, SUPERBLOCK_SIZE, BigHeap> BigSuperblockType;

  // The heap that manages large objects.
  class BigHeap :
    public ConformantHeap<HL::LockedHeap<TheLockType,
					 AddHeaderHeap<BigSuperblockType,
						       SUPERBLOCK_SIZE,
						       MmapSource > > >
  {
  };


  enum { BigObjectSize = 
	 HL::bins<SmallSuperblockType::Header, SUPERBLOCK_SIZE>::BIG_OBJECT };

  //
  // Each thread has its own heap for small objects.
  //
  class PerThreadHoardHeap :
    public RedirectFree<LockMallocHeap<SmallHeap>,
			SmallSuperblockType> {};
  

  template <int N, int NH>
  class HoardHeap :
    public HL::ANSIWrapper<
    IgnoreInvalidFree<
      HL::HybridHeap<Hoard::BigObjectSize,
		     ThreadPoolHeap<N, NH, Hoard::PerThreadHoardHeap>,
		     Hoard::BigHeap> > >
  {
  public:
    
    enum { BIG_OBJECT = Hoard::BigObjectSize };
    
    HoardHeap() {
      enum { BIG_HEADERS = sizeof(Hoard::BigSuperblockType::Header),
	     SMALL_HEADERS = sizeof(Hoard::SmallSuperblockType::Header)};
      HL::sassert<(BIG_HEADERS == SMALL_HEADERS)> ensureSameSizeHeaders;
    }
  };

}


#endif
