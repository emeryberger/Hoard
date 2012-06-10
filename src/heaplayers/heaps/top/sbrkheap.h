/* -*- C++ -*- */

#ifndef HL_SBRKHEAP_H
#define HL_SBRKHEAP_H

#ifdef WIN32

// If we're using Windows, we'll need to link in sbrk.c,
// a replacement for sbrk().

extern "C" void * sbrk (size_t sz);

#endif

/*
 * @class SbrkHeap
 * @brief A source heap that is a thin wrapper over sbrk.
 *
 * As it stands, memory cannot be returned to sbrk().
 * This is not a significant limitation, since only memory
 * at the end of the break point can ever be returned anyway.
 */

namespace HL {

  class SbrkHeap {
  public:

    enum { Alignment = 16 };

    SbrkHeap (void)
    {
      void * ptr = sbrk(0);
      while ((size_t) ptr % Alignment != 0) {
	ptr = sbrk(1);
      }
    }
    
    inline void * malloc (size_t sz) {
      if (sz == 0) {
	sz = Alignment;
      } else {
	while (sz % Alignment != 1) {
	  sz++;
	}
      }
      return sbrk(sz);
    }
    inline void free (void *) { }
  };

}


#endif

