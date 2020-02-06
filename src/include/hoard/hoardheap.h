// -*- C++ -*-

/*

  The Hoard Multiprocessor Memory Allocator
  www.hoard.org

  Author: Emery Berger, http://www.emeryberger.com
  Copyright (c) 1998-2020 Emery Berger

  See the LICENSE file at the top-level directory of this
  distribution and at http://github.com/emeryberger/Hoard.

*/

#ifndef HOARD_HOARDHEAP_H
#define HOARD_HOARDHEAP_H

#include <assert.h>

#include "heaplayers.h"

using namespace HL;

// The minimum allocation grain for a given object -
// that is, we carve objects out of chunks of this size.


#if defined(_WIN32)
// Larger superblock sizes are not yet working for Windows for some reason to be determined.
#define SUPERBLOCK_SIZE 65536UL
#else
#define SUPERBLOCK_SIZE 262144UL
// unclear why this is not working with 524288UL and larger...
#endif

//#define SUPERBLOCK_SIZE (256*1048576)
//#define SUPERBLOCK_SIZE (512*1048576)
//#define SUPERBLOCK_SIZE (1024UL*1048576)

// The number of 'emptiness classes'; see the ASPLOS paper for details.
#define EMPTINESS_CLASSES 8


// Hoard-specific layers

#include "thresholdheap.h"
#include "hoardmanager.h"
#include "addheaderheap.h"
#include "threadpoolheap.h"
#include "redirectfree.h"
#include "ignoreinvalidfree.h"
#include "conformantheap.h"
#include "hoardsuperblock.h"
#include "hoardsuperblockheader.h"
#include "lockmallocheap.h"
#include "alignedsuperblockheap.h"
#include "alignedmmap.h"
#include "globalheap.h"

#include "thresholdsegheap.h"
#include "geometricsizeclass.h"

// Note from Emery Berger: I plan to eventually eliminate the use of
// the spin lock, since the right place to do locking is in an
// OS-supplied library, and platforms have substantially improved the
// efficiency of these primitives.

#if defined(_WIN32)
typedef HL::WinLockType TheLockType;
#elif defined(__APPLE__)
// NOTE: On older versions of the Mac OS, Hoard CANNOT use Posix locks,
// since they may call malloc themselves. However, as of Snow Leopard,
// that problem seems to have gone away. Nonetheless, we use Mac-specific locks.
typedef HL::MacLockType TheLockType;
#elif defined(__SVR4)
typedef HL::SpinLockType TheLockType;
#else
typedef HL::SpinLockType TheLockType;
#endif

#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-variable"
#endif

namespace Hoard {

  class MmapSource : public AlignedMmap<SUPERBLOCK_SIZE, TheLockType> {};
  
  //
  // There is just one "global" heap, shared by all of the per-process heaps.
  //

  typedef GlobalHeap<SUPERBLOCK_SIZE, HoardSuperblockHeader, EMPTINESS_CLASSES, MmapSource, TheLockType>
  TheGlobalHeap;
  
  //
  // When a thread frees memory and causes a per-process heap to fall
  // below the emptiness threshold given in the function below, it
  // moves a (nearly or completely empty) superblock to the global heap.
  //

  class hoardThresholdFunctionClass {
  public:
    inline static bool function (unsigned int u,
				 unsigned int a,
				 size_t objSize)
    {
      /*
	Returns 1 iff we've crossed the emptiness threshold:
	
	U < A - 2S   &&   U < EMPTINESS_CLASSES-1/EMPTINESS_CLASSES * A
	
      */
      auto r = ((EMPTINESS_CLASSES * u) < ((EMPTINESS_CLASSES-1) * a)) && ((u < a - (2 * SUPERBLOCK_SIZE) / objSize));
      return r;
    }
  };
  

  class SmallHeap;
  
  //  typedef Hoard::HoardSuperblockHeader<TheLockType, SUPERBLOCK_SIZE, SmallHeap> HSHeader;
  typedef HoardSuperblock<TheLockType, SUPERBLOCK_SIZE, SmallHeap, Hoard::HoardSuperblockHeader> SmallSuperblockType;

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
		 SmallHeap> > 
  {};

  class BigHeap;

  typedef HoardSuperblock<TheLockType,
			  SUPERBLOCK_SIZE,
			  BigHeap,
			  Hoard::HoardSuperblockHeader>
  BigSuperblockType;

  // The heap that manages large objects.

#if 0

  // Old version: slow and now deprecated. Returns every large object
  // back to the system immediately.
  typedef ConformantHeap<HL::LockedHeap<TheLockType,
					AddHeaderHeap<BigSuperblockType,
						      SUPERBLOCK_SIZE,
						      MmapSource > > >
  bigHeapType;

#else

  // Experimental faster support for large objects.  MUCH MUCH faster
  // than the above (around 400x in some tests).  Keeps the amount of
  // retained memory at no more than X% more than currently allocated.

  class objectSource : public AddHeaderHeap<BigSuperblockType,
					    SUPERBLOCK_SIZE,
					    MmapSource> {};

  typedef HL::ThreadHeap<64, HL::LockedHeap<TheLockType,
					    ThresholdSegHeap<25,      // % waste
							     1048576, // at least 1MB in any heap
							     80,      // num size classes
							     GeometricSizeClass<20>::size2class,
							     GeometricSizeClass<20>::class2size,
							     GeometricSizeClass<20>::MaxObjectSize,
							     AdaptHeap<DLList, objectSource>,
							     objectSource> > >
  bigHeapType;
#endif

  class BigHeap : public bigHeapType {};

  enum { BigObjectSize = 
	 HL::bins<SmallSuperblockType::Header, SUPERBLOCK_SIZE>::BIG_OBJECT };

  //
  // Each thread has its own heap for small objects.
  //
  class PerThreadHoardHeap :
    public RedirectFree<LockMallocHeap<SmallHeap>,
			SmallSuperblockType> {
  private:
    void nothing() {
      _dummy[0] = _dummy[0];
    }
    // Avoid false sharing.
    char _dummy[64];
  };
  

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
      static_assert(BIG_HEADERS == SMALL_HEADERS,
		    "Headers must be the same size.");
    }
  };

}

#if defined(__clang__)
#pragma clang diagnostic pop
#endif

#endif
