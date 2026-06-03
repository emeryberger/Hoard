/* -*- C++ -*- */

/*

  The Hoard Multiprocessor Memory Allocator
  www.hoard.org

  Author: Emery Berger, http://www.emeryberger.com
  Copyright (c) 1998-2020 Emery Berger

  See the LICENSE file at the top-level directory of this
  distribution and at http://github.com/emeryberger/Hoard.

*/

/*
 * @file   libhoard.cpp
 * @brief  This file replaces malloc etc. in your application.
 * @author Emery Berger <http://www.emeryberger.com>
 */

#include <cstddef>
#include <cstring>
#include <new>

#include "VERSION.h"

// Enable custom realloc implementation to avoid redundant size lookups
#define HL_USE_XXREALLOC 1

#define versionMessage "Using the Hoard memory allocator (http://www.hoard.org), version " HOARD_VERSION_STRING "\n"

// Disable size checks in ANSIwrapper.
#define HL_NO_MALLOC_SIZE_CHECKS 0

#include "heaplayers.h"

// The undef below ensures that any pthread_* calls get strong
// linkage.  Otherwise, our versions here won't replace them.  It is
// IMPERATIVE that this line appear before any files get included.

#undef __GXX_WEAK__ 

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

// Maximize the degree of inlining.
#pragma inline_depth(255)

// Turn inlining hints into requirements.
#define inline __forceinline
#pragma warning(disable:4273)
#pragma warning(disable: 4098)  // Library conflict.
#pragma warning(disable: 4355)  // 'this' used in base member initializer list.
#pragma warning(disable: 4074)	// initializers put in compiler reserved area.
#pragma warning(disable: 6326)  // comparison between constants.

#endif

#if HOARD_NO_LOCK_OPT
// Disable lock optimization.
volatile bool anyThreadCreated = true;
#else
// The normal case. See heaplayers/spinlock.h.
volatile bool anyThreadCreated = false;
#endif

#include "hoardtlab.h"

//
// The base Hoard heap.
//


/// Maintain a single instance of the main Hoard heap.

Hoard::HoardHeapType * getMainHoardHeap() {
  // This function is C++ magic that ensures that the heap is
  // initialized before its first use. First, allocate a static buffer
  // to hold the heap.

  static double thBuf[sizeof(Hoard::HoardHeapType) / sizeof(double) + 1];

  // Now initialize the heap into that buffer.
  static auto * th = new (thBuf) Hoard::HoardHeapType;
  return th;
}

TheCustomHeapType * getCustomHeap();

enum { MAX_LOCAL_BUFFER_SIZE = 256 * 131072 };
static char initBuffer[MAX_LOCAL_BUFFER_SIZE];
static char * initBufferPtr = initBuffer;

extern bool isCustomHeapInitialized();

#include "wrappers/generic-memalign.cpp"

extern "C" {

#if defined(__GNUG__) || defined(__clang__)
  __attribute__((flatten)) __attribute__((alloc_size(1))) __attribute__((malloc))
  void * xxmalloc (size_t sz)
#else
  void * xxmalloc (size_t sz)
#endif
  {
    // Single TLS lookup - getCustomHeap returns nullptr if not initialized
    auto * heap = getCustomHeap();
    if (heap != nullptr) {
      void * ptr = heap->malloc(sz);
      if (ptr == nullptr) {
	fprintf(stderr, "INTERNAL FAILURE.\n");
	abort();
      }
      return ptr;
    }
    // We still haven't initialized the heap. Satisfy this memory
    // request from the local buffer.
    void * ptr = initBufferPtr;
    initBufferPtr += sz;
    if (initBufferPtr > initBuffer + MAX_LOCAL_BUFFER_SIZE) {
      abort();
    }
    {
      static bool initialized = false;
      if (!initialized) {
	initialized = true;
#if !defined(_WIN32)
	/* fprintf(stderr, versionMessage); */
#endif
      }
    }
    return ptr;
  }

#if defined(__GNUG__) || defined(__clang__)
  __attribute__((flatten))
  void xxfree (void * ptr)
#else
  void xxfree (void * ptr)
#endif
  {
    // Check init buffer first (cold path)
    if (__builtin_expect(ptr >= initBuffer && ptr < initBuffer + MAX_LOCAL_BUFFER_SIZE, 0)) {
      return;
    }
    auto * heap = getCustomHeap();
    if (__builtin_expect(heap != nullptr, 1)) {
      heap->free(ptr);
    }
  }

  void xxfree_sized(void * ptr, size_t) {
    xxfree(ptr);
  }

  void xxfree_aligned_sized(void * ptr, size_t, size_t) {
    xxfree(ptr);
  }

#if defined(__GNUG__)
  void * xxmemalign (size_t alignment, size_t sz) {
#else
  void * xxmemalign (size_t alignment, size_t sz) {
#endif
    return generic_xxmemalign(alignment, sz);
  }

  size_t xxmalloc_usable_size (void * ptr) {
    // Handle init buffer pointers
    if (ptr >= initBuffer && ptr < initBuffer + MAX_LOCAL_BUFFER_SIZE) {
      return static_cast<size_t>((initBuffer + MAX_LOCAL_BUFFER_SIZE) - (char*)ptr);
    }
    auto * heap = getCustomHeap();
    if (heap != nullptr) {
      return heap->getSize(ptr);
    }
    return 0;
  }

  void * xxrealloc(void * ptr, size_t sz) {
    // Handle null pointer - just malloc
    if (ptr == nullptr) {
      return xxmalloc(sz);
    }

    // Handle zero size - free and return null (POSIX behavior)
    if (sz == 0) {
      xxfree(ptr);
      return nullptr;
    }

    // Handle init buffer pointers specially
    if (ptr >= initBuffer && ptr < initBuffer + MAX_LOCAL_BUFFER_SIZE) {
      void * newPtr = xxmalloc(sz);
      if (newPtr) {
        size_t oldSize = static_cast<size_t>((initBuffer + MAX_LOCAL_BUFFER_SIZE) - (char*)ptr);
        std::memcpy(newPtr, ptr, oldSize < sz ? oldSize : sz);
      }
      return newPtr;
    }

    // Get old size once
    size_t oldSize = xxmalloc_usable_size(ptr);

    // If new size fits in old allocation, return original pointer
    if (sz <= oldSize) {
      return ptr;
    }

    // Allocate new block
    void * newPtr = xxmalloc(sz);
    if (newPtr == nullptr) {
      return nullptr;
    }

    // Copy old data and free old block
    std::memcpy(newPtr, ptr, oldSize);
    xxfree(ptr);

    return newPtr;
  }

  void xxmalloc_lock() {
    // Undefined for Hoard.
  }

  void xxmalloc_unlock() {
    // Undefined for Hoard.
  }

  // alloc8 expects xxcalloc
  void * xxcalloc(size_t count, size_t size) {
    // Overflow check
    size_t total = count * size;
    if (size != 0 && total / size != count) {
      return nullptr;
    }
    void * ptr = xxmalloc(total);
    if (ptr != nullptr) {
      std::memset(ptr, 0, total);
    }
    return ptr;
  }

} // extern "C"

// Note: alloc8 handles all malloc/free interposition via ALLOC8_INTERPOSE_SOURCES
