// -*- C++ -*-

#ifndef _OBSTACKREAP_H_
#define _OBSTACKREAP_H_

#include <assert.h>

/*

  ObstackReap layers obstack functionality on top of reaps.

  */

#if WIN32
#include <windows.h>
#endif

#include "dynarray.h"

namespace ObstackReapNS {

#if 0
	template <class ObjType>
  class DynamicArray {
  public:
    DynamicArray (void)
      : internalArray (0),
	internalArrayLength (0)
    {}
    
    ~DynamicArray (void)
    {
      clear();
    }
    
    // clear deletes everything in the array.

    inline void clear (void) {
      if (internalArray) {
	delete [] internalArray;
	internalArray = 0;
	internalArrayLength = 0;
	//printf ("\ninternalArrayLength %x = %d\n", this, internalArrayLength);
      }
    }

    // Read-only access to an array element;
    // asserts that this index is in range.

    inline const ObjType& operator[] (int index) const {
      assert (index < internalArrayLength);
      assert (index >= 0);
      return internalArray[index];
    }

    // Access a particular array index by reference,
    // growing the array if necessary.

    inline ObjType& operator[] (int index) {
      assert (index >= 0);
      if (index >= internalArrayLength) {

	// This index is beyond the current size of the array.
	// Grow the array by doubling and copying the old array into the new.

	const int newSize = index * 2 + 1;
	ObjType * arr = new ObjType[newSize];
	// printf ("grow! %d to %d\n", internalArrayLength, newSize);
#if MALLOC_TRACE
	printf ("m %x %d\n", arr, newSize * sizeof(ObjType));
#endif
	if (internalArray) {
	  memcpy (arr, internalArray, internalArrayLength * sizeof(ObjType));
	  delete [] internalArray;
#if MALLOC_TRACE
	  printf ("f %x\n", internalArray);
#endif
	}
	internalArray = arr;
	internalArrayLength = newSize;
	// printf ("\ninternalArrayLength %x = %d\n", this, internalArrayLength);
      }
      return internalArray[index];
    }

    // trim informs the array that it is now only nelts long
    // as far as the client is concerned. This may trigger
    // shrinking of the array.

    inline void trim (int nelts) {

      // Halve the array if the number of elements
      // drops below one-fourth of the array size.

      if (internalArray) {
	if (nelts * 4 < internalArrayLength) {
	  const int newSize = nelts * 2;
	  ObjType * arr = new ObjType[newSize];
	  // printf ("trim! %d to %d\n", internalArrayLength, newSize);
#if MALLOC_TRACE
	  printf ("m %x %d\n", arr, newSize * sizeof(ObjType));
#endif
	  memcpy (arr, internalArray, sizeof(ObjType) * nelts);
	  delete [] internalArray;
#if MALLOC_TRACE
	  printf ("f %x\n", internalArray);
#endif
	  internalArray = arr;
	  internalArrayLength = newSize;
	}
	assert (nelts <= internalArrayLength);
      }
    }


  private:

    // The pointer to the current array.

    ObjType * internalArray;

    // The length of the internal array, in elements.
	
    int internalArrayLength;
  };
#endif

  template <class OBJTYPE>
  class DynStack {
  public:
    DynStack (void)
      : numItems (0)
    {
      items[0] = 0;
    }

    int length (void) const { return numItems; }

    inline void push (OBJTYPE * ptr) {
      numItems++;
      items[numItems] = ptr;
    }

    inline OBJTYPE * pop (void) {
      OBJTYPE * ptr = 0;
      if (numItems > 0) {
	ptr = items[numItems];
	numItems--;
	// The array has shrunk, so potentially trim it.
	items.trim (numItems + 1);
      }
      return ptr;
    }

    inline OBJTYPE * top (void) {
      OBJTYPE * ptr = NULL;
      if (numItems > 0) {
	ptr = items[numItems];
      }
      return ptr;
    }

    inline void clear (void) {
      items.clear();
    }
  
  private:
  
    // The number of items recorded above.
    // 0 == no items.
    // 1 == items[1] has the single item, etc.
    // i.e., we waste entry zero.

    int numItems;

    // The array of remembered objects.

    DynamicArray<OBJTYPE *> items;
  };

};


// Layers on top of a reap.

template <class ReapType>
class ObstackReap {
public:

  ObstackReap (void)
  {
    currentReap = new ReapType;
    initCurrentObject();
  }

  ~ObstackReap (void) {
    ReapType * r;
    while ((r = reapStack.pop())) {
      delete r;
    }
    delete currentReap;
    delete currentObject;
  }
  
  inline void * malloc (size_t sz);
  inline void freeAfter (void * ptr);
  inline void freeAll (void);
  inline void * getObjectBase (void);
  inline void finalize (void);
  inline void * grow (size_t sz);

private:

  inline void initCurrentObject (void);

  // Hide free.
  void free (void *);

  enum { INITIAL_OBJECT_SIZE = 8 * sizeof(double) };

  void * currentObject;
  char * currentObjectPosition;
  size_t currentObjectSize;
  size_t actualObjectSize;
  bool isCurrentObjectExposed;
  ReapType * currentReap;
  ObstackReapNS::DynStack<ReapType> reapStack;
};


template <class ReapType>
void ObstackReap<ReapType>::initCurrentObject (void) {
  currentObject = currentReap->malloc (INITIAL_OBJECT_SIZE);
  currentObjectPosition = (char *) currentObject;
  currentObjectSize = 0;
  actualObjectSize = INITIAL_OBJECT_SIZE;
  isCurrentObjectExposed = false;
}

template <class ReapType>
void * ObstackReap<ReapType>::malloc (size_t sz) {
  if (!isCurrentObjectExposed) {
    return currentReap->malloc (sz);
  } else {
    void * ptr = currentReap->realloc (currentObject, sz);
    reapStack.push (currentReap);
    currentReap = new ReapType;
    initCurrentObject();
    return ptr;
  }
}

template <class ReapType>
void ObstackReap<ReapType>::freeAfter (void * ptr) {
  while (currentReap && (!currentReap->find(ptr))) {
    delete currentReap;
    currentReap = reapStack.pop();
  }
}

template <class ReapType>
void ObstackReap<ReapType>::freeAll (void) {
  while (currentReap) {
    delete currentReap;
    currentReap = reapStack.pop();
  }
  currentHeap = new ReapType;
}


template <class ReapType>
inline void * ObstackReap<ReapType>::getObjectBase (void) {
  isCurrentObjectExposed = true;
  return currentObject;
}

template <class ReapType>
inline void ObstackReap<ReapType>::finalize (void) {
  if (isCurrentObjectExposed) {
    reapStack.push (currentReap);
    currentReap = new ReapType;
  }
  initCurrentObject();
}

template <class ReapType>
inline void * ObstackReap<ReapType>::grow (size_t sz) {

  const int requestedObjectSize = currentObjectSize + sz;

  if (requestedObjectSize > actualObjectSize) {
    cout << "resize!\n";
    void * ptr = currentReap->realloc (currentObject, sz);
    currentObjectPosition = (char *) ptr + (currentObjectPosition - (char *) currentObject);
    if (isCurrentObjectExposed) {
      reapStack.push (currentReap);
      currentReap = new ReapType;
    }
    currentObject = ptr;
  }

  // Because calling grow can result in a new object,
  // the current object can be considered no longer exposed (if it was before).

  isCurrentObjectExposed = false;
  currentObjectSize += sz;
  char * oldPosition = currentObjectPosition;
  currentObjectPosition += sz;
  return oldPosition;
}

#endif
