// -*- C++ -*-

/*

  The Hoard Multiprocessor Memory Allocator
  www.hoard.org

  Author: Emery Berger, http://www.emeryberger.com
  Copyright (c) 1998-2020 Emery Berger

  See the LICENSE file at the top-level directory of this
  distribution and at http://github.com/emeryberger/Hoard.

*/

#ifndef HOARD_HEAPMANAGER_H
#define HOARD_HEAPMANAGER_H

#include <cstdlib>
#include <mutex>

#include "hoardconstants.h"
#include "heaplayers.h"

namespace Hoard {

  template <typename LockType,
	    typename HeapType>
  class HeapManager : public HeapType {
  public:

    enum { Alignment = HeapType::Alignment };

    HeapManager()
    {
      std::lock_guard<LockType> g (heapLock);
      
      /// Initialize all heap maps (nothing yet assigned).
      for (auto i = 0; i < HeapType::MaxThreads; i++) {
	HeapType::setTidMap (i, 0);
      }
      for (auto i = 0; i < HeapType::MaxHeaps; i++) {
	HeapType::setInusemap (i, 0);
      }
    }

    /// Set this thread's heap id to 0.
    void chooseZero() {
      std::lock_guard<LockType> g (heapLock);
      HeapType::setTidMap (HL::CPUInfo::getThreadId() % Hoard::MaxThreads, 0);
    }

    int findUnusedHeap() {

      std::lock_guard<LockType> g (heapLock);
      
      auto tid_original = HL::CPUInfo::getThreadId();
      auto tid = tid_original % HeapType::MaxThreads;
      
      int i = 0;
      while ((i < HeapType::MaxHeaps) && (HeapType::getInusemap(i)))
	i++;
      if (i >= HeapType::MaxHeaps) {
	// Every heap is in use: pick a random heap.
#if defined(_WIN32)
	auto randomNumber = rand();
#else
	auto randomNumber = (int) lrand48();
#endif
	i = randomNumber % HeapType::MaxHeaps;
      }

      HeapType::setInusemap (i, 1);
      HeapType::setTidMap ((int) tid, i);
      
      return i;
    }

    void releaseHeap() {
      // Decrement the ref-count on the current heap.
      
      std::lock_guard<LockType> g (heapLock);
      
      // Statically ensure that the number of threads is a power of two.
      enum { VerifyPowerOfTwo = 1 / ((HeapType::MaxThreads & ~(HeapType::MaxThreads-1))) };
      
      auto tid = (int) (HL::CPUInfo::getThreadId() & (HeapType::MaxThreads - 1));
      auto heapIndex = HeapType::getTidMap (tid);
      
      HeapType::setInusemap (heapIndex, 0);
      
      // Prevent underruns (defensive programming).
      
      if (HeapType::getInusemap (heapIndex) < 0) {
	HeapType::setInusemap (heapIndex, 0);
      }
    }
    
    
  private:
    
    // Disable copying.
    
    HeapManager (const HeapManager&);
    HeapManager& operator= (const HeapManager&);
    
    /// The lock, to ensure mutual exclusion.
    LockType heapLock;
  };

}

#endif
