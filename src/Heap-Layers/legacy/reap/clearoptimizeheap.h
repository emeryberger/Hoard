#if !defined(_CLEAROPTIMIZEHEAP_H_)
#define _CLEAROPTIMIZEHEAP_H_

/**
 * @class ClearOptimizeHeap
 * @brief A heap layer that is optimized for region-like allocation patterns.
 * @param Heap1 This heap provides memory for freshly allocated objects.
 * @param Heap2 This heap provides memory for recycled objects.
 */

template <class Heap1, class Heap2>
class ClearOptimizeHeap: public Heap1, Heap2 {
public:

  inline ClearOptimizeHeap (void)
    : nothingOnHeap (true)
  {
  }
  
  inline void * malloc (const size_t sz) {
    if (nothingOnHeap) {
      return Heap1::malloc (sz);
    } else {
      void * ptr = Heap2::malloc (sz);
	if (ptr == NULL) {
	  // Right now we assume that this means that the heap is in fact
	  // exhausted. This is not necessarily true, but it's a reasonable
	  // approximation, and is a lot cheaper than tracking the complete
	  // state of the parent heap.
	  nothingOnHeap = true;
	  ptr = Heap1::malloc (sz);
	}
	return ptr;
    }
  }
  
  inline void free (void * ptr) {
#if 1
    nothingOnHeap = false;
#else
    if (nothingOnHeap) {
      nothingOnHeap = false;
    }
#endif
    Heap2::free (ptr);
  }
  
  inline int remove (void * ptr) {
    return Heap2::remove (ptr); // heap2.remove (ptr);
  }
  
  inline void clear (void) {
    Heap1::clear();
    if (!nothingOnHeap) {
      Heap2::clear(); ///heap2.clear();
	}
    nothingOnHeap = true;
  }

  inline size_t getSize (const void * ptr) {
    return Heap2::getSize (ptr);
  }

private:

  /// True iff there is nothing on heap 2.
#ifdef WIN32
  BOOL nothingOnHeap;
#else
  bool nothingOnHeap;
#endif
  //  Heap2 heap2;
};

#endif
