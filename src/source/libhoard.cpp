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

#include <atomic>
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

// On Linux, use inline TLS access for fast path (defined in inlinetls.h)
// This avoids function call overhead on every malloc/free
#if defined(__linux__) || defined(__FreeBSD__) || defined(__NetBSD__)
#include "inlinetls.h"
#endif

// On macOS, inline the TLS fast path here as well (see mactlsfast.h).
#if defined(__APPLE__)
#include "mactlsfast.h"
#endif

// The xx* entry points are called only from the interposition layer linked
// into this same library. Hidden visibility keeps them out of the dynamic
// symbol table and lets LTO inline them into replace_malloc/replace_free.
//
// This matters just as much on ELF as on Mach-O, and for an extra reason: a
// default-visibility symbol in a shared object is *preemptible*, so the
// compiler must emit a PLT indirection for it and cannot inline through it.
// Left exported, `malloc` compiled to nothing but `b xxmalloc@plt` -- every
// allocation paid an indirect jump and the whole TLAB fast path stayed
// out-of-line behind it.
#if defined(__APPLE__) || defined(__ELF__)
#define HOARD_HOOK __attribute__((visibility("hidden")))
#else
#define HOARD_HOOK   // Windows: exported from the DLL.
#endif

//
// The base Hoard heap.
//


/// Maintain a single instance of the main Hoard heap.
///
/// Deliberately NOT a guarded function-local static ("magic static").
/// The heap constructor registers static destructors with the CRT; on
/// Windows the CRT grows its onexit table with recalloc, which Detours
/// routes straight back into Hoard while the guarded static would still
/// be marked "initialization in progress" - MSVC's init guard then
/// waits on its condition variable for its own thread, deadlocking
/// every injected process inside DllMain (issue #100; observed in the
/// CI stack captures as SleepConditionVariableSRW under
/// register_onexit_function/recalloc under LdrpInitializeProcess).
///
/// Instead: constant-initialized atomics (no guard), and re-entrant or
/// concurrent callers during construction get nullptr, which makes the
/// entry points fall back to the static init buffer below - exactly
/// what it exists for.

Hoard::HoardHeapType * getMainHoardHeap() {
  // Zero-initialized static buffer: no init guard. Must carry the heap
  // type's alignment: placement-new into an under-aligned buffer is UB
  // (GCC on x86-64 turns the alignas promise into aligned vector
  // stores that fault).
  alignas(Hoard::HoardHeapType)
  static char thBuf[sizeof(Hoard::HoardHeapType)];
  // Constant-initialized (C++20 P0883): no init guard.
  static std::atomic<Hoard::HoardHeapType *> th { nullptr };
  static std::atomic<bool> constructing { false };

  auto * p = th.load (std::memory_order_acquire);
  if (HL_EXPECT_TRUE(p != nullptr)) {
    return p;
  }
  if (constructing.exchange (true, std::memory_order_acq_rel)) {
    // Re-entered from within the constructor (e.g. a detoured CRT
    // allocation on Windows), or raced by another thread mid-init:
    // serve this request from the init buffer.
    return nullptr;
  }
  p = new (thBuf) Hoard::HoardHeapType;
  th.store (p, std::memory_order_release);
  return p;
}

TheCustomHeapType * getCustomHeap();

enum { MAX_LOCAL_BUFFER_SIZE = 256 * 131072 };
static char initBuffer[MAX_LOCAL_BUFFER_SIZE];
static char * initBufferPtr = initBuffer;

extern bool isCustomHeapInitialized();

#if !defined(_WIN32)
#include "util/ownershipmap.h"
// Storage for the ownership bitmap (see ownershipmap.h): an ordinary
// non-weak zero-initialized global so it lands in a zerofill segment —
// reserved address space only; pages materialize on first touch.
//
// Plain uint64_t, accessed via std::atomic_ref (see ownershipmap.h) —
// NOT std::atomic. An array of std::atomic cannot be constant-initialized
// with libc++ (its default constructor is not usable in a constant
// expression), so an unoptimized build emits a dynamic initializer that
// default-constructs all 2^24 atomics from __mod_init_func. That runs
// *after* Hoard has begun serving malloc (we interpose it, so dyld and
// other images' initializers allocate through us first), and it wipes the
// ownership bits of every superblock mapped during startup: alloc8 then
// sees those pointers as foreign and misroutes their free/malloc_size.
// It would also touch all 128MB, turning reserved address space into
// resident memory. A plain integer array has no constructor to run, so it
// lands in zerofill at every optimization level; constinit enforces that
// at compile time rather than leaving it to the optimizer.
#if defined(__APPLE__)
// Mach-O only: on ELF the storage is an inline variable in ownershipmap.h, so
// that consumers building Hoard's heaps from the headers alone still link.
namespace Hoard {
  namespace ownershipdetail {
    constinit uint64_t bits[kNumWords] = {};
  }
}
#endif
#endif

#include "wrappers/generic-memalign.cpp"

#if defined(__APPLE__) || defined(_WIN32)

// Ownership hook consumed by the alloc8 interposition layer. Providing
// these strong definitions makes alloc8 skip its internal per-pointer
// size table (a hash insert/erase on every malloc/free that saturates
// at ~4M live objects) and answer free/realloc/malloc_size ownership
// via Hoard's O(1) superblock ownership map instead.
//
// The fast path actually used with current alloc8 is the compile-time
// inline twin in hoardownsinline.h (via ALLOC8_XXOWNS_INLINE_HEADER);
// the runtime hook below remains for older alloc8 versions.

#include "hoardownsinline.h"

// hoardownsinline.h must agree with the real superblock size.
static_assert(HOARD_OWNS_CHUNK_SIZE == SUPERBLOCK_SIZE,
              "hoardownsinline.h chunk size out of sync with SUPERBLOCK_SIZE");

extern "C" {

  // Constant-initialized bounds for hoardownsinline.h (no dynamic
  // initializer: safe to read from arbitrarily early interposed calls).
  const char * const hoardInitBufferStart = initBuffer;
  const char * const hoardInitBufferEnd = initBuffer + MAX_LOCAL_BUFFER_SIZE;

  HOARD_HOOK bool xxowns_active() {
    return true;
  }

  HOARD_HOOK bool xxowns (const void * ptr) {
    if (HL_EXPECT_TRUE(Hoard::OwnershipMap<SUPERBLOCK_SIZE>::contains (ptr))) {
      return true;
    }
    // Allocations served from the static init buffer before heap
    // initialization are also ours.
    auto * p = reinterpret_cast<const char *>(ptr);
    return (p >= initBuffer && p < initBuffer + MAX_LOCAL_BUFFER_SIZE);
  }

}

#endif

extern "C" {

#if defined(__GNUG__) || defined(__clang__)
  __attribute__((alloc_size(1))) __attribute__((malloc))
  HOARD_HOOK void * xxmalloc (size_t sz)
#else
  HOARD_HOOK void * xxmalloc (size_t sz)
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

  HOARD_HOOK void xxfree (void * ptr)
  {
    if (HL_EXPECT_FALSE(ptr == nullptr)) {
      return;
    }
    // Check init buffer first (cold path)
    if (HL_EXPECT_FALSE(ptr >= initBuffer && ptr < initBuffer + MAX_LOCAL_BUFFER_SIZE)) {
      return;
    }

    // The TLAB and Hoard internals use normalize() to handle internal pointers.
    // This allows free() of pointers from aligned_alloc to work correctly.
    auto * heap = getCustomHeap();
    if (HL_EXPECT_TRUE(heap != nullptr)) {
      heap->free(ptr);
    }
  }

  HOARD_HOOK void xxfree_sized(void * ptr, size_t) {
    xxfree(ptr);
  }

  HOARD_HOOK void xxfree_aligned_sized(void * ptr, size_t, size_t) {
    xxfree(ptr);
  }

  /// Aligned allocation using Hoard's normalization.
  /// Hoard uses normalize() in the free path to round internal pointers
  /// back to their object start, so we can return internal pointers safely.
  HOARD_HOOK void * xxmemalign (size_t alignment, size_t sz) {
    // Check for non power-of-two alignment or zero.
    if ((alignment == 0) || (alignment & (alignment - 1))) {
      return nullptr;
    }

    // Alignment <= 8: every size class provides it.
    if (alignment <= 8) {
      return xxmalloc(sz);
    }

    // Alignment 16: size classes that are multiples of 16 are 16-byte
    // aligned (superblock data starts 16-aligned), so rounding the
    // request up to a multiple of 16 suffices. Do NOT rely on
    // alignof(max_align_t) here: with 8-byte-granularity size classes
    // (24, 40, ...), plain malloc only guarantees 8-byte alignment.
    if (alignment == 16) {
      sz = (sz + 15) & ~(size_t) 15;
      if (sz == 0) {
        sz = 16;
      }
      return xxmalloc(sz);
    }

    // Allocate enough space to satisfy alignment requirement.
    // The extra alignment bytes ensure we can find an aligned address within.
    size_t totalSize = sz + alignment;
    void* ptr = xxmalloc(totalSize);
    if (ptr == nullptr) {
      return nullptr;
    }

    // Calculate aligned address within the allocation.
    uintptr_t addr = reinterpret_cast<uintptr_t>(ptr);
    uintptr_t alignedAddr = (addr + alignment - 1) & ~(alignment - 1);

    // Hoard's free path uses normalize() which will round this back to ptr.
    return reinterpret_cast<void*>(alignedAddr);
  }

  HOARD_HOOK size_t xxmalloc_usable_size (void * ptr) {
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

  HOARD_HOOK void * xxrealloc(void * ptr, size_t sz) {
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

  HOARD_HOOK void xxmalloc_lock() {
    // Undefined for Hoard.
  }

  HOARD_HOOK void xxmalloc_unlock() {
    // Undefined for Hoard.
  }

  // alloc8 expects xxcalloc
  HOARD_HOOK void * xxcalloc(size_t count, size_t size) {
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
