/*

  The Hoard Multiprocessor Memory Allocator
  www.hoard.org

  Author: Emery Berger, http://www.emeryberger.com
  Copyright (c) 1998-2020 Emery Berger

  See the LICENSE file at the top-level directory of this
  distribution and at http://github.com/emeryberger/Hoard.

*/

#if !defined(__APPLE__)
#error "This file is intended for use with MacOS systems only."
#endif

#include <dlfcn.h>
#include <pthread.h>
#include <utility>

#include "Heap-Layers/heaplayers.h"
#include "hoard/hoardtlab.h"

extern Hoard::HoardHeapType * getMainHoardHeap();

static pthread_key_t theHeapKey;
static pthread_once_t key_once = PTHREAD_ONCE_INIT;

__thread TheCustomHeapType * per_thread_heap;

// Called when the thread goes away.  This function clears out the
// TLAB and then reclaims the memory allocated to hold it.

static void deleteThatHeap(void * p) {
  reinterpret_cast<TheCustomHeapType *>(p)->clear();
  getMainHoardHeap()->free(p);

  // Relinquish the assigned heap.
  getMainHoardHeap()->releaseHeap();
}

static void make_heap_key() {
  if (pthread_key_create(&theHeapKey, deleteThatHeap) != 0) {
    // This should never happen.
  }
}

static bool initializedTSD = false;

static bool initTSD() {
  if (!initializedTSD) {
    // Ensure that the key is initialized -- once.
    pthread_once(&key_once, make_heap_key);
    initializedTSD = true;
  }
  return true;
}

bool isCustomHeapInitialized() {
  return initializedTSD;
}

static TheCustomHeapType * initializeCustomHeap() {
  TheCustomHeapType * heap =
    reinterpret_cast<TheCustomHeapType *>(pthread_getspecific(theHeapKey));
  if (heap == nullptr) {
    // Defensive programming in case this is called twice.
    // Allocate a per-thread heap.
    size_t sz = sizeof(TheCustomHeapType);
    char * mh = reinterpret_cast<char *>(getMainHoardHeap()->malloc(sz));
    heap = new (mh) TheCustomHeapType(getMainHoardHeap());
    // Store it in the appropriate thread-local area.
    pthread_setspecific(theHeapKey, heap);
  }
  return heap;
}

TheCustomHeapType * getCustomHeap() {
  initTSD();
  // Allocate a per-thread heap.
  TheCustomHeapType * heap =
    reinterpret_cast<TheCustomHeapType *>(pthread_getspecific(theHeapKey));
  if (heap == nullptr)  {
    heap = initializeCustomHeap();
  }
  return heap;
}


//
// Intercept thread creation and destruction to flush the TLABs.
//


extern "C" {
  typedef void * (*threadFunctionType)(void * arg);
}

// A special routine we call on thread exits to free up some resources.
static void exitRoutine() {
  TheCustomHeapType * heap = getCustomHeap();

  // Clear the TLAB's buffer.
  heap->clear();

  // Relinquish the assigned heap.
  getMainHoardHeap()->releaseHeap();
}

extern "C" {
  static inline void * startMeUp(void * a) {
    // Make sure that the custom heap has been initialized,
    // then find an unused process heap for this thread, if possible.
    getCustomHeap();
    getMainHoardHeap()->findUnusedHeap();

    // Extract the pair elements (function, argument).
    pair<threadFunctionType, void *> * z
      = reinterpret_cast<pair<threadFunctionType, void *> *>(a);

    threadFunctionType fun = z->first;
    void * arg = z->second;

    // Execute the function.
    void * result = (*fun)(arg);

    // We're done: free up resources.
    exitRoutine();
    getCustomHeap()->free(a);
    return result;
  }
}


extern volatile bool anyThreadCreated;


// Intercept thread creation. We need this to first associate
// a heap with the thread and instantiate the thread-specific heap
// (TLAB).  When the thread ends, we relinquish the assigned heap and
// free up the TLAB.


extern "C" void xxpthread_exit(void * value_ptr) {
  // Do necessary clean-up of the TLAB and get out.
  exitRoutine();
  pthread_exit(value_ptr);
}

extern "C" int xxpthread_create(pthread_t *thread,
                                const pthread_attr_t *attr,
                                void * (*start_routine)(void *),
                                void * arg) {
  // Force initialization of the TLAB before our first thread is created.
  static TheCustomHeapType * t = getCustomHeap();

  anyThreadCreated = true;

  pair<threadFunctionType, void *> * args =
    new (t->malloc(sizeof(pair<threadFunctionType, void *>)))
    pair<threadFunctionType, void *>(start_routine, arg);

  int result = pthread_create(thread, attr, startMeUp, args);
  return result;
}


MAC_INTERPOSE(xxpthread_create, pthread_create);
MAC_INTERPOSE(xxpthread_exit, pthread_exit);



