// -*- C++ -*-

/*

  The Hoard Multiprocessor Memory Allocator
  www.hoard.org

  Author: Emery Berger, http://www.emeryberger.com
  Copyright (c) 1998-2020 Emery Berger

  See the LICENSE file at the top-level directory of this
  distribution and at http://github.com/emeryberger/Hoard.

*/

#ifndef HOARD_THREADPOOLHEAP_H
#define HOARD_THREADPOOLHEAP_H

#include <cassert>

#include "heaplayers.h"
#include "array.h"
//#include "cpuinfo.h"

namespace Hoard {

  template <int NumThreads,
	    int NumHeaps,
	    class PerThreadHeap_>
  class ThreadPoolHeap : public PerThreadHeap_ {
  public:
    
    typedef PerThreadHeap_ PerThreadHeap;
    
    enum { MaxThreads = NumThreads };
    enum { NumThreadsMask = NumThreads - 1};
    enum { NumHeapsMask = NumHeaps - 1};
    
    enum { MaxHeaps = NumHeaps };
    
    ThreadPoolHeap()
    {
      static_assert((NumHeaps & NumHeapsMask) == 0,
		    "Number of heaps must be a power of two.");
      static_assert((NumThreads & NumThreadsMask) == 0,
		    "Number of threads must be a power of two.");
    
      // Note: The tidmap values should be set externally.
      int j = 0;
      for (int i = 0; i < NumThreads; i++) {
	setTidMap(i, j % NumHeaps);
	j++;
      }
    }
    
    inline PerThreadHeap& getHeap (void) {
      auto tid = HL::CPUInfo::getThreadId();
      auto heapno = _tidMap(tid & NumThreadsMask);
      return _heap(heapno);
    }
    
    inline void * malloc (size_t sz) {
      return getHeap().malloc (sz);
    }
    
    inline void free (void * ptr) {
      getHeap().free (ptr);
    }
    
    inline void clear() {
      getHeap().clear();
    }
    
    inline size_t getSize (void * ptr) {
      return PerThreadHeap::getSize (ptr);
    }
    
    void setTidMap (int index, int value) {
      assert ((value >= 0) && (value < MaxHeaps));
      _tidMap(index) = value;
    }
    
    int getTidMap (int index) const {
      return _tidMap(index); 
    }
    
    void setInusemap (int index, int value) {
      _inUseMap(index) = value;
    }
    
    int getInusemap (int index) const {
      return _inUseMap(index);
    }
    
    
  private:
    
    /// Which heap is assigned to which thread, indexed by thread.
    Array<MaxThreads, int> _tidMap;
    
    /// Which heap is in use (a reference count).
    Array<MaxHeaps, int> _inUseMap;
    
    /// The array of heaps we choose from.
    Array<MaxHeaps, PerThreadHeap> _heap;
    
  };
  
}

#endif
