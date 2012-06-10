// -*- C++ -*-

/*

  Heap Layers: An Extensible Memory Allocation Infrastructure
  
  Copyright (C) 2000-2012 by Emery Berger
  http://www.cs.umass.edu/~emery
  emery@cs.umass.edu
  
  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
  (at your option) any later version.
  
  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.
  
  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
  
*/

/**
 * @file strictsegheap.h
 * @brief Definition of StrictSegHeap.
 */

#ifndef HL_STRICTSEGHEAP_H
#define HL_STRICTSEGHEAP_H

#include "segheap.h"

/**
 * @class StrictSegHeap
 * @brief A "strict" segregated-fits collection of (homogeneous) heaps.
 * 
 * One extra heap is used for objects that are "too big".  Unlike
 * SegHeap, StrictSegHeap does not perform splitting to satisfy memory
 * requests. If no memory is available from the appropriate bin,
 * malloc returns NULL.
 *
 * @sa SegHeap
 *
 * @param NumBins The number of bins (subheaps).
 * @param getSizeClass Function to compute size class from size.
 * @param getClassMaxSize Function to compute the largest size for a given size class.
 * @param LittleHeap The subheap class.
 * @param BigHeap The parent class, used for "big" objects.
 */

namespace HL {

  template <int NumBins,
	    int (*getSizeClass) (const size_t),
	    size_t (*getClassMaxSize) (const int),
	    class LittleHeap,
	    class BigHeap>
  class StrictSegHeap :
    public SegHeap<NumBins, getSizeClass, getClassMaxSize, LittleHeap, BigHeap>
  {
  private:

    typedef SegHeap<NumBins, getSizeClass, getClassMaxSize, LittleHeap, BigHeap> SuperHeap;

    typedef int (*scFunction) (const size_t);
    typedef size_t (*csFunction) (const int);

  public:

    void freeAll (void) {
      int i;
      for (i = 0; i < NumBins; i++) {
	const size_t sz = ((csFunction) getClassMaxSize)(i);
	void * ptr;
	while ((ptr = SuperHeap::myLittleHeap[i].malloc (sz)) != NULL) {
	  SuperHeap::bigheap.free (ptr);
	}
      }
      for (int j = 0; j < SuperHeap::NUM_ULONGS; j++) {
	SuperHeap::binmap[j] = 0;
      }
      SuperHeap::memoryHeld = 0;
    }

  
    /**
     * Malloc from exactly one available size.
     * (don't look in every small heap, as in SegHeap).
     */

    inline void * malloc (const size_t sz) {
      void * ptr = NULL;
      if (sz <= SuperHeap::maxObjectSize) {
	const int sizeClass = ((scFunction) getSizeClass) (sz);
	assert (sizeClass >= 0);
	assert (sizeClass < NumBins);
	ptr = SuperHeap::myLittleHeap[sizeClass].malloc (sz);
      }
      if (!ptr) {
	ptr = SuperHeap::bigheap.malloc (sz);
      }
      return ptr;
    }

    inline void free (void * ptr) {
      const size_t objectSize = SuperHeap::getSize(ptr);
      if (objectSize > SuperHeap::maxObjectSize) {
	SuperHeap::bigheap.free (ptr);
      } else {
	int objectSizeClass = ((scFunction) getSizeClass) (objectSize);
	assert (objectSizeClass >= 0);
	assert (objectSizeClass < NumBins);
	// Put the freed object into the right sizeclass heap.
	assert (getClassMaxSize(objectSizeClass) >= objectSize);
	while (((csFunction) getClassMaxSize)(objectSizeClass) > objectSize) {
	  objectSizeClass--;
	}
	assert (((csFunction) getClassMaxSize)(objectSizeClass) <= objectSize);
	if (objectSizeClass > 0) {
	  assert (objectSize >= ((csFunction) getClassMaxSize)(objectSizeClass - 1));
	}

	SuperHeap::myLittleHeap[objectSizeClass].free (ptr);
	SuperHeap::memoryHeld += objectSize;
      }
    }

  };

}

#endif

