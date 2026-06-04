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
//----------------------------------------------------------------------

#define HOARD_TLS_SLOT 89

#if defined(__x86_64__)

static inline TheCustomHeapType* getTlsHeap() {
  void* res;
  const size_t ofs = HOARD_TLS_SLOT * sizeof(void*);
  __asm__ volatile("movq %%gs:%1, %0" : "=r" (res) : "m" (*((void**)ofs)));
  return reinterpret_cast<TheCustomHeapType*>(res);
}

static inline void setTlsHeap(TheCustomHeapType* value) {
  const size_t ofs = HOARD_TLS_SLOT * sizeof(void*);
  __asm__ volatile("movq %1, %%gs:%0" : "=m" (*((void**)ofs)) : "r" ((void*)value));
}

#elif defined(__aarch64__)

static inline TheCustomHeapType* getTlsHeap() {
  void** tcb;
  __asm__ volatile("mrs %0, tpidrro_el0\n\tbic %0, %0, #7" : "=r" (tcb));
  return reinterpret_cast<TheCustomHeapType*>(tcb[HOARD_TLS_SLOT]);
}

static inline void setTlsHeap(TheCustomHeapType* value) {
  void** tcb;
  __asm__ volatile("mrs %0, tpidrro_el0\n\tbic %0, %0, #7" : "=r" (tcb));
  tcb[HOARD_TLS_SLOT] = value;
}

#else
// Fallback for other architectures - use pthread_getspecific
static inline TheCustomHeapType* getTlsHeap() {
  return reinterpret_cast<TheCustomHeapType*>(pthread_getspecific(theHeapKey));
}

static inline void setTlsHeap(TheCustomHeapType* value) {
  pthread_setspecific(theHeapKey, value);
}
#endif

// Called when the thread goes away.  This function clears out the
// TLAB and then reclaims the memory allocated to hold it.

static void deleteThatHeap(void * p) {
  reinterpret_cast<TheCustomHeapType *>(p)->clear();
  getMainHoardHeap()->free(p);

  // Relinquish the assigned heap.
  getMainHoardHeap()->releaseHeap();

  // Clear the TLS slot
  setTlsHeap(nullptr);
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
  // Allocate a per-thread heap.
  size_t sz = sizeof(TheCustomHeapType);
  char * mh = reinterpret_cast<char *>(getMainHoardHeap()->malloc(sz));
  TheCustomHeapType * heap = new (mh) TheCustomHeapType(getMainHoardHeap());
  // Store in both fast TLS slot (for hot path) and pthread_key (for destructor).
  setTlsHeap(heap);
  pthread_setspecific(theHeapKey, heap);
  return heap;
}

TheCustomHeapType * getCustomHeap() {
  // Fast path: direct TLS slot access (single memory load, no function call).
  TheCustomHeapType * heap = getTlsHeap();
  if (__builtin_expect(heap != nullptr, 1)) {
    return heap;
  }
  // Slow path: first access on this thread, initialize.
  initTSD();
  return initializeCustomHeap();
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
