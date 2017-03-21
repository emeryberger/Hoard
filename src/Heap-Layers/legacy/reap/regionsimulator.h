/* -*- C++ -*- */

/**
 * @file regionsimulator.h
 * @author Emery Berger
 *
 * An experimental layer for replacing the memory allocator used by
 * gcc (obstacks) and other region-based allocators with real malloc &
 * free calls.
 */


#ifndef _REGIONSIMULATOR_H_
#define _REGIONSIMULATOR_H_

#ifdef __cplusplus

#ifndef MALLOC_TRACE
#define MALLOC_TRACE 0
#endif


#include "..\heaplayers\traceheap.h"
#include "..\heaplayers\dynarray.h"

#include <assert.h>


#if 0
// A dynamic array that automatically grows to fit
// any index requested for assignment.
//
// This array also features a clear() method,
// to free the entire array, and a trim(n) method,
// which tells the array it is no bigger than n elements.

template <class ObjType>
class DynamicArray {
public:
  DynamicArray (void)
    : internalArray (NULL),
      internalArrayLength (0)
  {}

  ~DynamicArray (void)
  {
    clear();
  }

  // clear deletes everything in the array.

  inline void clear (void) {
    if (internalArray != NULL) {
      delete [] internalArray;
      internalArray = NULL;
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
      if (internalArray != NULL) {
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

    if (internalArray != NULL) {
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


#if 1
class MallocStack {
public:
  MallocStack (void)
    : numMallocs (0)
  {
    mallocs[0] = NULL;
  }

  int length (void) const { return numMallocs; }

  inline void push (void * ptr) {
    numMallocs++;
    mallocs[numMallocs] = ptr;
  }

  inline void * pop (void) {
    void * ptr = NULL;
    if (numMallocs > 0) {
      ptr = mallocs[numMallocs];
      numMallocs--;
      // The array has shrunk, so potentially trim it.
      mallocs.trim (numMallocs + 1);
    }
    return ptr;
  }

  inline void * top (void) {
    void * ptr = NULL;
    if (numMallocs > 0) {
      ptr = mallocs[numMallocs];
    }
    return ptr;
  }

  inline void clear (void) {
	  mallocs.clear();
  }
  
private:
  
  // The number of mallocs recorded above.
  // 0 == no mallocs.
  // 1 == mallocs[1] has the one malloc, etc.
  // i.e., we waste entry zero.

  int numMallocs;

  // The array of remembered malloc pointers.

  DynamicArray<void *> mallocs;


};
#else
class MallocStack {
public:

  inline int length (void) const {
    return theStack.size();
  }

  inline void push (void * p) {
    theStack.push_front(p);
  }

  inline void * top (void) const {
    if (length() <= 0) {
      return NULL;
    } else {
      return theStack.front();
    }
  }

  inline void * pop (void) {
    if (length() <= 0) {
      return NULL;
    }
    void * ptr = top();
    theStack.pop_front();
    return ptr;
  }

private:

  list<void *> theStack;

};

#endif


/**
 * @class RegionSimulator
 * @author Emery Berger
 *  RegionSimulator acts like a region (or obstack),
 * tracking all mallocs for a subsequent freeAll.
 * free emulates obstack frees by freeing all objects
 * allocated after a given point.
 *
 */

#if defined(_WIN32)
#include <windows.h>
#endif

template <class SuperHeap>
class RegionSimulator : public SuperHeap {
public:


  RegionSimulator (void)
    : magicNumber (MAGIC_NUMBER),
      parent (NULL),
      child (NULL),
      sibling (NULL)
  {
    initCurrentObject();
    sanityCheck();
  }

  ~RegionSimulator (void) {
    sanityCheck();

//	freeAll();
    SuperHeap::free (currentObject);

#if 1
    // Free everything still on our allocated object array.

#if 1
    void * ptr;
    while ((ptr = mallocStack.pop())) {
      if ((size_t) ptr & 1 == 0)
	SuperHeap::free (ptr);
    }
#endif

    // Now iterate over the children and destroy them all.

#if 1
    while (child != NULL) {
      RegionSimulator * nextChild = child->sibling;
      delete child;
      child = nextChild;
    }
#endif

    // Inform our parent that we're gone.

    if (parent != NULL) {
      parent->removeChild (this);
    }
#endif

  }

  // Make the current object big enough to accomodate this request.

  inline void * malloc (size_t sz)
  {
    sanityCheck();

    void * ptr;

    // If the current object has been exposed,
    // we need to grow it. Otherwise, we can allocate
    // the size requested directly.

    if (isCurrentObjectExposed) {

      // Round up the request size, and grow the current object
      // large enough to hold sz bytes.
      // Finalize and return the grown object.

      grow (sz - currentObjectSize);
      ptr = currentObject;
      assert (currentObjectSize >= sz);
      finalize();

    } else

      {

      // Allocate the object directly
      // and record it.

      ptr = SuperHeap::malloc (sz);
      mallocStack.push (ptr);

#if MALLOC_TRACE
      printf ("M %x %d\n", ptr, sz);
#endif
    }


    sanityCheck();
    return ptr;
  }


private:

  // Hide free.
  void free (void *);

public:

  // Obstack support.

  // Free every pointer allocated AFTER this pointer.
  inline void freeAfter (void * ptr) {
    sanityCheck();

    if (ptr != currentObject) {

      // Free everything, in reverse allocated order,
      // until we find ptr. We have to mask off a 1 when
      // we do these comparisons because we add 1 bits to
      // indicate when objects have already been freed.

      while ((mallocStack.top() != NULL)
	     && (((size_t) mallocStack.top() & ~1) != (size_t) ptr))
	{

	  void * popPtr = mallocStack.top();
	
	  // If this object has not yet been freed, free it.
	
	  if (((size_t) popPtr & 1) == 0) {
	    void * addr = (void *) ((size_t) popPtr & ~1);
#if MALLOC_TRACE
	    printf ("F %x\n", addr);
#endif
	    SuperHeap::free (addr);
	  }
	  mallocStack.pop();
	}

      sanityCheck();

    } else {

      // Just free the current object and start a new one.

#if MALLOC_TRACE
      printf ("M %x %d\n", currentObject, currentObjectSize);
      printf ("F %x\n", currentObject);
#endif
      SuperHeap::free (currentObject);
      initCurrentObject();
    }

  }


  // Free every pointer allocated to date.
  inline void freeAll (void)
  {
    void * ptr;
    sanityCheck();

    while ((ptr = mallocStack.pop())) {
//		printf ("F %x\n", ptr);
      if (((size_t) ptr & 1) == 0) {
	SuperHeap::free ((void *) ((size_t) ptr & ~1));
      }
    }
    mallocStack.clear();
    
    while (child != NULL) {
      RegionSimulator * nextChild = child->sibling;
      delete child;
      child = nextChild;
    }

#if 0
    RegionSimulator * ch = child;
    while (ch != NULL) {
      RegionSimulator * nextChild = ch->sibling;
      ch->freeAll();
      ch = nextChild;
    }
#endif

    sanityCheck();

  }
  
  // Return the start of the current allocated object.
  inline void * getObjectBase (void) {
    sanityCheck();
    isCurrentObjectExposed = true;
    return currentObject;
  }

  // Finalize an object.
  inline void finalize (void) {
    sanityCheck();

    // Add the current allocated object to our array
    // and start a new object.

    mallocStack.push (currentObject);

#if MALLOC_TRACE
    printf ("M %x %d\n", currentObject, currentObjectSize);
#endif

    initCurrentObject();

    sanityCheck();
  }

  // "Grow" the current object by a certain amount.

  inline void * grow (size_t sz) {

    sanityCheck();

    // Copy out the old object into a new one.
    // This is effectively a realloc.
    // If we actually still have space in our
    // current allocation, though, do nothing.

    const int requestedObjectSize = align(currentObjectSize + sz);

    if (requestedObjectSize > actualObjectSize) {

      // Get a new object and copy everything into it,
      // modifying CurrentObjectPosition so it points
      // into the right place in the new object.

      void * ptr = SuperHeap::malloc (requestedObjectSize);
#if MALLOC_TRACE
      printf ("m %x %d\n", ptr, requestedObjectSize);
#endif

      actualObjectSize = requestedObjectSize;
      if (ptr == NULL)
	abort();
      memcpy (ptr, currentObject, currentObjectSize);
      SuperHeap::free (currentObject);
#if MALLOC_TRACE
      printf ("f %x\n", currentObject);
#endif

      currentObjectPosition = (char *) ptr + (currentObjectPosition - (char *) currentObject);

      // If someone has already obtained a "handle" on the current object
      // via getObjectBase, record this object as already freed -- we may
      // have to free backwards to this pointer.

      if (isCurrentObjectExposed) {
			
	// Record this object.
	// We OR the address with 1 to mark this object as already freed.
			
	mallocStack.push ((void *) ((size_t) currentObject | 1));
      }

      currentObject = ptr;
    }

    // We've "grown" the object, so we update its perceived size,
    // advance the current position so it points to the previous
    // starting position plus the size. Return the previous starting position.

    // Because calling grow can result in a new object,
    // the current object can be considered no longer exposed (if it was before).

    isCurrentObjectExposed = false;

    currentObjectSize += sz;
    char * oldPosition = currentObjectPosition;
    currentObjectPosition += sz;
    sanityCheck();
    return oldPosition;
  }

  // ch should be a new region, i.e., it should not have any siblings yet.

  void addChild (RegionSimulator * ch)
  {
    if (child == NULL) {
      child = ch;
    } else {
      assert (ch->sibling == NULL);
      ch->sibling = child;
      child = ch;
    }
    ch->parent = this;
  }

private:

#if 1

  void removeChild (RegionSimulator * ch)
  {
    assert (ch != NULL);
    if (child == ch) {
      child = child->sibling;
    } else {
      child->removeSibling (ch);
    }
  }

  void removeSibling (RegionSimulator * sib)
  {
	RegionSimulator * prev = NULL;
	RegionSimulator * curr = sibling;
	int i = 0;
	while (curr && (curr != sib)) {
		prev = curr;
		curr = curr->sibling;
		i++;
	}
	if (prev == NULL) {
		sibling = sibling->sibling;
	} else {
		prev->sibling = curr->sibling;
	}
  }

  RegionSimulator * parent;
  RegionSimulator * child;
  RegionSimulator * sibling;
#endif

  enum { INITIAL_OBJECT_SIZE = sizeof(double) };


  // Allocate a new object and initialize the associated variables.

  inline void initCurrentObject (void) {
    currentObject = SuperHeap::malloc (INITIAL_OBJECT_SIZE);
    if (currentObject == NULL)
      abort();
    currentObjectPosition = (char *) currentObject;
    currentObjectSize = 0;
    actualObjectSize = INITIAL_OBJECT_SIZE;
    isCurrentObjectExposed = false;
  }

  // Round up to a double word.

  inline static size_t align (int sz) {
    return (sz + (sizeof(double) - 1)) & ~(sizeof(double) - 1);
  }

  inline void sanityCheck (void) {

    // Check some class invariants:
    //
    // - Verify that we really are in a RegionSimulator layer
    //   by checking a magic number.
    // - The current object must exist.
    // - The current position needs to be after the start of the object.

    assert (magicNumber == MAGIC_NUMBER);
    assert (currentObject != NULL);
    assert (currentObjectPosition >= (char *) currentObject);
  }

  // With this magic number, we're either in this layer
  // or we've found ourselves in a Java class file :).

  enum { MAGIC_NUMBER = 0xCafeBabe };

  // The current object, the current position in that object,
  // and the current object's perceived and actual sizes.

  void * currentObject;
  char * currentObjectPosition;
  size_t currentObjectSize;
  size_t actualObjectSize;

  // Have we exposed the start of this object to the outside world?
  // If we have, we will have to record this object in the mallocs array
  // even if we free it.

  bool isCurrentObjectExposed;

  // A magic number just for sanity checking.

  int magicNumber;

  // The stack of malloc'ed objects.

  MallocStack mallocStack;

};



#if 0
extern char * PseudoObstackName;
template <class SuperHeap>
class PseudoObstack : public RegionSimulator<SuperHeap> {};
#endif

#define C_LINKAGE extern "C"

#else  /* __cplusplus */

#define C_LINKAGE extern

#endif /* __cplusplus */

#include <stdlib.h>

/* C_LINKAGE void regionCreate (void ** reg); */
C_LINKAGE void regionCreate (void ** reg, void ** parent);
C_LINKAGE void regionDestroy (void ** reg);
C_LINKAGE void * regionAllocate (void ** reg, size_t sz);
C_LINKAGE void regionFreeAll (void ** reg);

/* Obstack functions */

struct obstack {
	void * theObstack;
};

C_LINKAGE void obstack_init (struct obstack *obstack);
C_LINKAGE void * obstack_alloc (struct obstack *obstack, size_t size);
C_LINKAGE void * obstack_copy (struct obstack *obstack, void *address, size_t size);
C_LINKAGE void * obstack_copy0 (struct obstack *obstack, void *address, size_t size);
C_LINKAGE void obstack_free (struct obstack *obstack, void *block);
C_LINKAGE void obstack_blank (struct obstack *obstack, size_t size);
C_LINKAGE void obstack_grow (struct obstack *obstack, void *data, size_t size);
C_LINKAGE void obstack_grow0 (struct obstack *obstack, void *data, int size);
C_LINKAGE void obstack_1grow (struct obstack *obstack, long c);
C_LINKAGE void obstack_ptr_grow (struct obstack *obstack, void *data);
C_LINKAGE void obstack_int_grow (struct obstack *obstack, int data);
C_LINKAGE void * obstack_finish (struct obstack * obstack);
C_LINKAGE void * obstack_base (struct obstack *obstack);
C_LINKAGE void * obstack_next_free (struct obstack *obstack);
C_LINKAGE int _obstack_get_alignment (struct obstack *);

#endif /* _REGIONSIMULATOR_H_ */

