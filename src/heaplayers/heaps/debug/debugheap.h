/* -*- C++ -*- */


#ifndef HL_DEBUGHEAP_H_
#define HL_DEBUGHEAP_H_

#include <assert.h>

/**
 *
 *
 */

namespace HL {

template <class Super,
	  char freeChar = 'F'>
class DebugHeap : public Super {
private:

  enum { CANARY = 0xdeadbeef };

public:

  // Fill with A's.
  inline void * malloc (size_t sz) {
    // Add a guard area at the end.
    void * ptr;
    ptr = Super::malloc (sz + sizeof(unsigned long));
    for (unsigned long i = 0; i < sz; i++) {
      ((char *) ptr)[i] = 'A';
    }
    *((unsigned long *) ((char *) ptr + sz)) = (unsigned long) CANARY;
    assert (Super::getSize(ptr) >= sz);
    return ptr;
  }
  
  // Fill with F's.
  inline void free (void * ptr) {
    char * b = (char *) ptr;
    size_t sz = Super::getSize(ptr);
    // Check for the canary.
    unsigned long storedCanary = *((unsigned long *) b + sz - sizeof(unsigned long));
    if (storedCanary != CANARY) {
      abort();
    }
    for (unsigned int i = 0; i < sz; i++) {
      ((char *) ptr)[i] = freeChar;
    }
    Super::free (ptr);
  }
};

}

#endif
