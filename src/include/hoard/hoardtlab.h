// -*- C++ -*-

/*

  The Hoard Multiprocessor Memory Allocator
  www.hoard.org

  Author: Emery Berger, http://www.emeryberger.com
  Copyright (c) 1998-2020 Emery Berger

  See the LICENSE file at the top-level directory of this
  distribution and at http://github.com/emeryberger/Hoard.

*/

/**
 * @file   hoardtlab.h
 * @brief  Definitions for the Hoard thread-local heap.
 * @author Emery Berger <http://www.emeryberger.com>
 * @note   Copyright (C) 2010-2020 by Emery Berger.
 */


#ifndef HOARD_HOARDTLAB_H
#define HOARD_HOARDTLAB_H

#include "hoardheap.h"
#include "heapmanager.h"
#include "tlab.h"
#include "hoardconstants.h"

#include "heaplayers.h"
#include "../util/bins256k.h"

namespace Hoard {
  
  // HOARD_MMAP_PROTECTION_MASK defines the protection flags used for
  // freshly-allocated memory. The default case is that heap memory is
  // NOT executable, thus preventing the class of attacks that inject
  // executable code on the heap.
  // 
  // While this is not recommended, you can define HL_EXECUTABLE_HEAP as
  // 1 in heaplayers/heaplayers.h if you really need to (i.e., you're
  // doing dynamic code generation into malloc'd space).
  
#if HL_EXECUTABLE_HEAP
#define HOARD_MMAP_PROTECTION_MASK (PROT_READ | PROT_WRITE | PROT_EXEC)
#else
#define HOARD_MMAP_PROTECTION_MASK (PROT_READ | PROT_WRITE)
#endif

  //
  // The base Hoard heap.
  //
  
  class HoardHeapType :
    public HeapManager<TheLockType, HoardHeap<MaxThreads, NumHeaps> > {
  };
  
  // Just an abbreviation.
  typedef HoardHeapType::SuperblockType::Header TheHeader;
  
  //
  // The thread-local 'allocation buffers' (TLABs), which is a bit of a
  // misnomer since these are actually separate heaps in their own
  // right.
  //

  typedef ThreadLocalAllocationBuffer<HL::bins<TheHeader, SUPERBLOCK_SIZE>::NUM_BINS,
				      HL::bins<TheHeader, SUPERBLOCK_SIZE>::getSizeClass,
				      HL::bins<TheHeader, SUPERBLOCK_SIZE>::getClassSize,
				      LargestSmallObject,
				      MAX_MEMORY_PER_TLAB,
				      HoardHeapType::SuperblockType,
				      SUPERBLOCK_SIZE,
				      HoardHeapType>
  TLABBase;
  
}

// The per-thread heap type used by the malloc/free entry points.
// ANSIWrapper supplies getSize/realloc/calloc plumbing; malloc and
// free are overridden with force-inlined versions that go straight to
// the TLAB. Without this, LTO keeps ANSIWrapper::malloc as an
// out-of-line call on the hottest path in the allocator.
class TheCustomHeapType : public HL::ANSIWrapper<Hoard::TLABBase> {
  typedef HL::ANSIWrapper<Hoard::TLABBase> Parent;
public:
  using Parent::Parent;

  TLAB_ALWAYS_INLINE void * malloc (size_t sz) {
    return Hoard::TLABBase::malloc (sz);
  }

  TLAB_ALWAYS_INLINE void free (void * ptr) {
    if (HL_EXPECT_TRUE(ptr != nullptr)) {
      Hoard::TLABBase::free (ptr);
    }
  }
};

#endif
