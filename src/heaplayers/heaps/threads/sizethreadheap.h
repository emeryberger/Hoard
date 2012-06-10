/* -*- C++ -*- */

#ifndef _SIZETHREADHEAP_H_
#define _SIZETHREADHEAP_H_

#include <assert.h>
#include "cpuinfo.h"

template <class Super>
class SizeThreadHeap : public Super {
public:
  
  inline void * malloc (size_t sz) {
    // Add room for a size field & a thread field.
	// Both of these must fit in a double.
	assert (sizeof(st) <= sizeof(double));
    st * ptr = (st *) Super::malloc (sz + sizeof(double));
    // Store the requested size.
    ptr->size = sz;
	assert (getOrigPtr(ptr + 1) == ptr);
    return (void *) (ptr + 1);
  }
  
  inline void free (void * ptr) {
    void * origPtr = (void *) getOrigPtr(ptr);
    Super::free (origPtr);
  }

  static inline size_t& size (void * ptr) {
		return getOrigPtr(ptr)->size;
  }

  static inline int& thread (void * ptr) {
		return getOrigPtr(ptr)->tid;
	}

private:

	typedef struct _st {
		size_t size;
		int tid;
	} st;

	static inline st * getOrigPtr (void * ptr) {
		return (st *) ((double *) ptr - 1);
	}

};



// A platform-dependent way to get a thread id.

// Include the necessary platform-dependent crud.
#if defined(WIN32) || defined(__WIN32__) || defined(_WIN32)
#ifndef WIN32
#define WIN32 1
#endif
#include <windows.h>
#include <process.h>
#endif



#endif
