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

#include <atomic>
#include <cassert>

#if defined(__linux__)
#include <sched.h>
#elif defined(__APPLE__)
#include <pthread.h>
#endif

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
#if !defined(HOARD_DISABLE_CPU_HEAP_SELECTION)
#if defined(__linux__)
      // Use CPU-based selection for NUMA locality.
      // Threads on the same CPU use the same heap, reducing cross-node traffic.
      int cpu = sched_getcpu();
      if (cpu >= 0) {
        return _heap(cpu & NumHeapsMask);
      }
#elif defined(__APPLE__)
      // pthread_cpu_number_np is available since macOS 11.0 and is very fast
      // (~2ns, no syscall - reads from thread-local commpage).
      size_t cpu;
      if (pthread_cpu_number_np(&cpu) == 0) {
        return _heap(static_cast<int>(cpu) & NumHeapsMask);
      }
#endif
#endif
      // Fallback: hash thread ID to heap
      auto tid = HL::CPUInfo::getThreadId();
      auto heapno = _tidMap(tid & NumThreadsMask);
      return _heap(heapno);
    }

    /// Assign a home heap for a new thread's TLAB. The TLAB refills
    /// exclusively from its home heap for its whole lifetime, which
    /// gives every thread a STABLE ownership identity: the TLAB free
    /// path compares superblock owners against the home heap to
    /// classify frees as local or foreign, and that predicate must not
    /// shift when the scheduler moves the thread between CPUs (a
    /// CPU-keyed heap would reclassify the thread's entire working set
    /// on every migration and churn it through remote frees).
    ///
    /// Round-robin: consecutively created threads get distinct heaps
    /// until the counter wraps at NumHeaps. A dead thread's heap keeps
    /// its superblocks; once its objects are freed back (via the
    /// delayed-free queue and the locked free path), empty superblocks
    /// flow to the global heap for reuse by later heaps.
    inline PerThreadHeap& assignHomeHeap() {
      // Constant-initialized: no static init guard (see getMainHoardHeap).
      static std::atomic<unsigned int> nextHome;
      auto i = nextHome.fetch_add (1, std::memory_order_relaxed);
      return _heap((int) (i & NumHeapsMask));
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
