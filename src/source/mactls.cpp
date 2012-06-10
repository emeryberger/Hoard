#if !defined(__APPLE__)
#error "This file is intended for use with MacOS systems only."
#endif

/*
  The Hoard Multiprocessor Memory Allocator
  www.hoard.org

  Author: Emery Berger, http://www.cs.umass.edu/~emery
 
  Copyright (c) 1998-2012 Emery Berger

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

#include "macinterpose.h"
#include "hoardtlab.h"

using namespace Hoard;

extern HoardHeapType * getMainHoardHeap (void);

#include <pthread.h>

static pthread_key_t theHeapKey;
static pthread_once_t key_once = PTHREAD_ONCE_INIT;

// Called when the thread goes away.  This function clears out the
// TLAB and then reclaims the memory allocated to hold it.

static void deleteThatHeap (void * p) {
  TheCustomHeapType * heap = (TheCustomHeapType *) p;
  heap->clear();
  getMainHoardHeap()->free ((void *) heap);
  
  // Relinquish the assigned heap.
  getMainHoardHeap()->releaseHeap();
}

static void make_heap_key (void)
{
  if (pthread_key_create (&theHeapKey, deleteThatHeap) != 0) {
    // This should never happen.
  }
}

static bool initTSD (void) {
  static bool initializedTSD = false;
  if (!initializedTSD) {
    // Ensure that the key is initialized -- once.
    pthread_once (&key_once, make_heap_key);
    initializedTSD = true;
  }
  return true;
}

static TheCustomHeapType * initializeCustomHeap (void)
{
  TheCustomHeapType * heap =
    (TheCustomHeapType *) pthread_getspecific (theHeapKey);
  if (heap == NULL) {
    // Defensive programming in case this is called twice.
    // Allocate a per-thread heap.
    size_t sz = sizeof(TheCustomHeapType);
    void * mh = getMainHoardHeap()->malloc(sz);
    heap = new ((char *) mh) TheCustomHeapType (getMainHoardHeap());
    // Store it in the appropriate thread-local area.
    pthread_setspecific (theHeapKey, (void *) heap);
  }
  return heap;
}

TheCustomHeapType * getCustomHeap (void) {
  initTSD();
  // Allocate a per-thread heap.
  TheCustomHeapType * heap =
    (TheCustomHeapType *) pthread_getspecific (theHeapKey);
  if (heap == NULL)  {
    heap = initializeCustomHeap();
  }
  return heap;
}


//
// Intercept thread creation and destruction to flush the TLABs.
//


extern "C" {

  typedef void * (*threadFunctionType) (void *);


}

// A special routine we call on thread exits to free up some resources.
static void exitRoutine (void) {
  TheCustomHeapType * heap = getCustomHeap();

  // Clear the TLAB's buffer.
  heap->clear();

  // Relinquish the assigned heap.
  getMainHoardHeap()->releaseHeap();
}

extern "C" {

  static inline void * startMeUp (void * a)
  {
    getCustomHeap();
    getMainHoardHeap()->findUnusedHeap();
    pair<threadFunctionType, void *> * z
      = (pair<threadFunctionType, void *> *) a;
    
    threadFunctionType f = z->first;
    void * arg = z->second;

    void * result = NULL;
    result = (*f)(arg);
    exitRoutine();
    
    getCustomHeap()->free (a);
    return result;
  }
  
}

#include <dlfcn.h>

extern volatile bool anyThreadCreated;


// Intercept thread creation. We need this to first associate
// a heap with the thread and instantiate the thread-specific heap
// (TLAB).  When the thread ends, we relinquish the assigned heap and
// free up the TLAB.


extern "C" void xxpthread_exit (void *value_ptr) {

  // Do necessary clean-up of the TLAB and get out.
  exitRoutine();
  pthread_exit (value_ptr);

}

extern "C" int xxpthread_create (pthread_t *thread,
				 const pthread_attr_t *attr,
				 void * (*start_routine) (void *),
				 void * arg)
{
  // Force initialization of the TLAB before our first thread is created.
  static TheCustomHeapType * t = getCustomHeap();

  anyThreadCreated = true;

  pair<threadFunctionType, void *> * args =
    new (t->malloc (sizeof(pair<threadFunctionType, void *>)))
    pair<threadFunctionType, void *> (start_routine, arg);

  int result = pthread_create (thread, attr, startMeUp, args);
  return result;
}


MAC_INTERPOSE(xxpthread_create, pthread_create);
MAC_INTERPOSE(xxpthread_exit, pthread_exit);



