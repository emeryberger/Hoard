/* -*- C++ -*- */

#ifndef _BOUNDEDFREELISTHEAP_H_
#define _BOUNDEDFREELISTHEAP_H_

// Beware -- this is for one "size class" only!!

template <int numObjects, class Super>
class BoundedFreeListHeap : public Super {
public:
  
  BoundedFreeListHeap (void)
    : nObjects (0),
    myFreeList (NULL)
  {}

  ~BoundedFreeListHeap (void)
  {
    clear();
  }

  inline void * malloc (size_t sz) {
    // Check the free list first.
    void * ptr = myFreeList;
    if (ptr == NULL) {
      ptr = Super::malloc (sz);
    } else {
      myFreeList = myFreeList->next;
    }
    return ptr;
  }
  
  inline void free (void * ptr) {
    if (nObjects < numObjects) {
      // Add this object to the free list.
      ((freeObject *) ptr)->next = myFreeList;
      myFreeList = (freeObject *) ptr;
      nObjects++;
    } else {
      clear();
      //      Super::free (ptr);
    }
  }

  inline void clear (void) {
    // Delete everything on the free list.
    void * ptr = myFreeList;
    while (ptr != NULL) {
      void * oldptr = ptr;
      ptr = (void *) ((freeObject *) ptr)->next;
      Super::free (oldptr);
    }
    myFreeList = NULL;
    nObjects = 0;
  }

private:

  class freeObject {
  public:
    freeObject * next;
  };

  int nObjects;
  freeObject * myFreeList;
};

#endif
