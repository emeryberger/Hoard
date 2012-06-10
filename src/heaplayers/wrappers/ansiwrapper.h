/* -*- C++ -*- */

#ifndef HL_ANSIWRAPPER_H
#define HL_ANSIWRAPPER_H

#include <assert.h>
#include <string.h>

#include "gcd.h"
#include "istrue.h"
#include "sassert.h"
#include "mallocinfo.h"


/*
 * @class ANSIWrapper
 * @brief Provide ANSI C behavior for malloc & free.
 *
 * Implements all prescribed ANSI behavior, including zero-sized
 * requests & aligned request sizes to a double word (or long word).
 */

namespace HL {

  template <class SuperHeap>
  class ANSIWrapper : public SuperHeap {
  public:
  
    ANSIWrapper() {
      sassert<(gcd<SuperHeap::Alignment, HL::MallocInfo::Alignment>::value == HL::MallocInfo::Alignment)> checkAlignment;
    }

    inline void * malloc (size_t sz) {
      // Prevent integer underflows. This maximum should (and
      // currently does) provide more than enough slack to compensate for any
      // rounding below (in the alignment section).
      if (sz > HL::MallocInfo::MaxSize) {
	return NULL;
      }
      if (sz < HL::MallocInfo::MinSize) {
      	sz = HL::MallocInfo::MinSize;
      }
      // Enforce alignment requirements: round up allocation sizes if needed.
      if ((size_t) SuperHeap::Alignment < (size_t) HL::MallocInfo::Alignment) {
	sz += HL::MallocInfo::Alignment - (sz % HL::MallocInfo::Alignment);
      }
      void * ptr = SuperHeap::malloc (sz);
      assert ((size_t) ptr % HL::MallocInfo::Alignment == 0);
      return ptr;
    }
 
    inline void free (void * ptr) {
      if (ptr != 0) {
      	SuperHeap::free (ptr);
      }
    }

    inline void * calloc (const size_t s1, const size_t s2) {
      char * ptr = (char *) malloc (s1 * s2);
      if (ptr) {
      	memset (ptr, 0, s1 * s2);
      }
      return (void *) ptr;
    }
  
    inline void * realloc (void * ptr, const size_t sz) {
      if (ptr == 0) {
      	return malloc (sz);
      }
      if (sz == 0) {
      	free (ptr);
      	return 0;
      }

      size_t objSize = getSize (ptr);
      if (objSize == sz) {
    	return ptr;
      }

      // Allocate a new block of size sz.
      void * buf = malloc (sz);

      // Copy the contents of the original object
      // up to the size of the new block.

      size_t minSize = (objSize < sz) ? objSize : sz;
      if (buf) {
	memcpy (buf, ptr, minSize);
      }

      // Free the old block.
      free (ptr);
      return buf;
    }
  
    inline size_t getSize (void * ptr) {
      if (ptr) {
	return SuperHeap::getSize (ptr);
      } else {
	return 0;
      }
    }
  };

}

#endif
