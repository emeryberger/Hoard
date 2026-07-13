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

#include <unistd.h>
#include <dlfcn.h>
#include <pthread.h>
#include <utility>

#include "heaplayers.h"
#include "hoard/hoardtlab.h"
#include "hoard/mactlsfast.h"

// Required by the replacement printf library (https://github.com/emeryberger/printf)
extern "C" {
  void _putchar(char ch) {
    write(STDOUT_FILENO, &ch, 1);
  }
}

extern Hoard::HoardHeapType * getMainHoardHeap();

static pthread_key_t theHeapKey;
static pthread_once_t key_once = PTHREAD_ONCE_INIT;

//----------------------------------------------------------------------
// Fast TLS slot access (mimalloc-style optimization)
//
// On macOS, __thread variables go through _tlv_get_addr which is slow.
// Instead, we directly access an unused pthread TLS slot (slot 89).
// This gives us a single memory load instead of a function call.
//
// The accessors (hoardGetTlsHeap/hoardSetTlsHeap) and the inline
// getCustomHeap() fast path live in hoard/mactlsfast.h so that every
// caller (notably the malloc/free entry points in libhoard.cpp) can
// inline them; only the first-access slow path lives here.
//----------------------------------------------------------------------

static inline TheCustomHeapType* getTlsHeap() {
  return hoardGetTlsHeap();
}

static inline void setTlsHeap(TheCustomHeapType* value) {
  hoardSetTlsHeap(value);
}

// Called when the thread goes away. This clears out the TLAB and then
// reclaims the memory allocated to hold it. It also clears both TLS
// locations so explicit pthread_exit cleanup and pthread-key cleanup do
// not run twice for the same thread.
static void destroyThatHeap(TheCustomHeapType * heap) {
  if (heap == nullptr) {
    return;
  }
  heap->clear();

  // CPU-based heap selection does not use the thread heap map, so avoid
  // taking the global heap-map lock on every thread exit in that mode.
#if defined(HOARD_DISABLE_CPU_HEAP_SELECTION)
  getMainHoardHeap()->releaseHeap();
#endif

  pthread_setspecific(theHeapKey, nullptr);
  setTlsHeap(nullptr);
  getMainHoardHeap()->free(heap);
}

static void deleteThatHeap(void * p) {
  destroyThatHeap(reinterpret_cast<TheCustomHeapType *>(p));
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
  // Allocate a per-thread heap. The main heap is nullptr while it is
  // still under construction (re-entrant allocation from within its
  // constructor); the caller then falls back to the init buffer.
  auto * mainHeap = getMainHoardHeap();
  if (mainHeap == nullptr) {
    return nullptr;
  }
  size_t sz = sizeof(TheCustomHeapType);
  char * mh = reinterpret_cast<char *>(mainHeap->malloc(sz));
  if (mh == nullptr) {
    return nullptr;
  }
  TheCustomHeapType * heap = new (mh) TheCustomHeapType(mainHeap);
  // Store in both fast TLS slot (for hot path) and pthread_key (for destructor).
  setTlsHeap(heap);
  pthread_setspecific(theHeapKey, heap);
  return heap;
}

// Slow path for the inline getCustomHeap() in mactlsfast.h:
// first access on this thread, initialize.
TheCustomHeapType * hoardSlowGetCustomHeap() {
  initTSD();
  return initializeCustomHeap();
}


//
// Intercept thread creation and destruction to flush the TLABs.
//


extern "C" {
  typedef void * (*threadFunctionType)(void * arg);
}

// A special routine we call on explicit pthread_exit and on normal returns
// from the wrapper. Threads not created through our wrapper are cleaned by
// the pthread-key destructor above.
static void exitRoutine() {
  destroyThatHeap(getTlsHeap());
}

extern "C" {
  static inline void * startMeUp(void * a) {
    // Make sure that the custom heap has been initialized; exitRoutine()
    // below handles normal returns, while xxpthread_exit handles explicit
    // pthread_exit() from inside the user function.
    getCustomHeap();

#if defined(HOARD_DISABLE_CPU_HEAP_SELECTION)
    getMainHoardHeap()->findUnusedHeap();
#endif

    // Extract the pair elements (function, argument).
    pair<threadFunctionType, void *> * z
      = reinterpret_cast<pair<threadFunctionType, void *> *>(a);

    threadFunctionType fun = z->first;
    void * arg = z->second;

    // Execute the function.
    void * result = (*fun)(arg);

    getCustomHeap()->free(a);
    exitRoutine();
    return result;
  }
}


extern volatile bool anyThreadCreated;


// Intercept thread creation. We need this to first associate
// a heap with the thread and instantiate the thread-specific heap
// (TLAB).  When the thread ends, we relinquish the assigned heap and
// free up the TLAB.


extern "C" void xxpthread_exit(void * value_ptr) {
  // Clean up before pthread_exit tears the thread down; exitRoutine clears
  // the pthread key so the key destructor will not repeat the work.
  exitRoutine();
  pthread_exit(value_ptr);
}

extern "C" int xxpthread_create(pthread_t *thread,
                                const pthread_attr_t *attr,
                                void * (*start_routine)(void *),
                                void * arg) {
  // Must be the *calling* thread's heap, fetched on every call: a static
  // would pin every future pthread_create to the first creator's TLAB, which
  // is destroyed when that thread exits (threads here spawn other threads).
  TheCustomHeapType * t = getCustomHeap();

  anyThreadCreated = true;

  pair<threadFunctionType, void *> * args =
    new (t->malloc(sizeof(pair<threadFunctionType, void *>)))
    pair<threadFunctionType, void *>(start_routine, arg);

  int result = pthread_create(thread, attr, startMeUp, args);
  return result;
}


MAC_INTERPOSE(xxpthread_create, pthread_create);
MAC_INTERPOSE(xxpthread_exit, pthread_exit);
