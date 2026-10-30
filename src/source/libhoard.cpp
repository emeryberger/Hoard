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
#include <cstdint>
#include <cerrno>
#include <cstring>
#include <new>

#include "VERSION.h"

// Largest request Hoard will attempt to satisfy. Defined once in
// hoardconstants.h (Hoard::MaxAllocationSize) and enforced both here and in
// TheCustomHeapType::malloc; see that header for the rationale.
#define HOARD_MAX_ALLOCATION (Hoard::MaxAllocationSize)

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

// NOTE: do NOT `#define inline __forceinline` here.
//
// It was legacy and redundant -- Heap-Layers already maps INLINE to
// __forceinline on MSVC (and has its own copy of this #define commented out),
// and every hot path force-inlines explicitly (INLINE, TLAB_ALWAYS_INLINE, or
// a bare __forceinline). What the macro DID do was rewrite `inline` on *data*
// declarations, which MSVC rejects outright:
//
//   error C2433: '__forceinline' not permitted on data declarations
//
// so no header reachable from here could use a C++17 `inline` variable or a
// `static inline` member. That trap cost three separate build breakages
// (ownershipmap.h's bitmap, sizeclasses.h's tables, shardedglobalheap.h's
// retain counter), each caught only by Windows CI. Removing it lets headers
// use ordinary modern C++.
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

  // --- Over-aligned allocation recovery (alignment >= SUPERBLOCK_SIZE) ------
  //
  // A pointer aligned to >= SUPERBLOCK_SIZE is itself superblock-aligned, so
  // SuperblockType::getSuperblock() (a mask down to the superblock base) maps
  // it to itself -- an address that holds object data, not a superblock
  // header -- and Hoard's normalize()-based free cannot recover the
  // allocation from it. For such requests xxmemalign stashes the canonical
  // (Hoard-issued) pointer in the word immediately before the returned
  // pointer; the free / usable-size / realloc paths recover it here.
  //
  // Detection is cheap and safe. A genuine Hoard object pointer is never
  // superblock-aligned -- every object starts sizeof(Header) past a
  // superblock base and none spans a whole superblock back to a boundary --
  // so a superblock-aligned pointer that Hoard owns can only be one of these
  // relocated allocations. Ownership of the byte just below the pointer is
  // confirmed via the O(1) ownership map BEFORE any dereference, so a stray
  // superblock-aligned pointer into foreign or unmapped memory is never read.
  //
  // Platform-independent: the ownership map (OwnershipMap<SUPERBLOCK_SIZE>) and
  // the superblock accessors are the same on every platform -- Windows just
  // uses a smaller SUPERBLOCK_SIZE (64KB) and a two-level ownership radix, both
  // of which this code picks up automatically.
  typedef Hoard::HoardHeapType::SuperblockType HoardSB;

  // Returns the canonical Hoard pointer for a relocated over-aligned pointer,
  // or nullptr if `ptr` is not one. Cold + noinline: callers gate it behind a
  // single superblock-alignment test, so the fast path pays only that test.
#if defined(__GNUG__) || defined(__clang__)
  __attribute__((noinline, cold))
#else
  NO_INLINE
#endif
  static void * hoardRecoverOverAligned (void * ptr) {
    const uintptr_t a = reinterpret_cast<uintptr_t>(ptr);
    if (a == 0 || (a & (SUPERBLOCK_SIZE - 1)) != 0) {
      return nullptr;            // not superblock-aligned: an ordinary pointer
    }
    // The stash sits in the word just below `ptr`; confirm that byte is
    // Hoard's before touching it (never dereference foreign/unmapped memory).
    if (!Hoard::OwnershipMap<SUPERBLOCK_SIZE>::contains (
            reinterpret_cast<const void *>(a - 1))) {
      return nullptr;
    }
    void * canonical = *reinterpret_cast<void **>(a - sizeof(void *));
    // The canonical pointer must be a Hoard-owned address strictly below
    // `ptr`, with a valid superblock header, whose object actually covers
    // `ptr` (the last test also defends against a corrupted stash word).
    if (canonical == nullptr ||
        reinterpret_cast<uintptr_t>(canonical) >= a ||
        !Hoard::OwnershipMap<SUPERBLOCK_SIZE>::contains (canonical)) {
      return nullptr;
    }
    HoardSB * s = HoardSB::getSuperblock (canonical);
    if (s == nullptr || !s->isValidSuperblock()) {
      return nullptr;
    }
    const size_t objBytes = s->getSize (canonical);
    if (objBytes == 0 ||
        (a - reinterpret_cast<uintptr_t>(canonical)) >= objBytes) {
      return nullptr;
    }
    return canonical;
  }

#if defined(__GNUG__) || defined(__clang__)
  __attribute__((alloc_size(1))) __attribute__((malloc))
  HOARD_HOOK void * xxmalloc (size_t sz)
#else
  HOARD_HOOK void * xxmalloc (size_t sz)
#endif
  {
    // Reject oversized requests up front. This is the single choke point
    // for every allocation entry point (malloc, calloc, realloc, memalign
    // all funnel here), so one check guards them all. It must live here and
    // not rely on ANSIWrapper::malloc: the hot-path TheCustomHeapType::malloc
    // deliberately bypasses ANSIWrapper (calling TLABBase::malloc directly),
    // so the wrapper's only overflow guard never runs on the fast path.
    // Without this, a size within sizeof(Header) of SIZE_MAX wraps when the
    // big-object layer adds its header, mapping a tiny superblock while
    // getSize() reports the astronomical requested size -- a heap buffer
    // overflow (CWE-190 -> CWE-787).
    if (HL_EXPECT_FALSE(sz > HOARD_MAX_ALLOCATION)) {
      errno = ENOMEM;
      return nullptr;
    }
    // Single TLS lookup - getCustomHeap returns nullptr if not initialized
    auto * heap = getCustomHeap();
    if (heap != nullptr) {
      void * ptr = heap->malloc(sz);
      // A null result here means the OS could not back the request (OOM).
      // malloc's contract is to return null on failure; aborting would turn
      // any large/unsatisfiable request into a process kill (DoS).
      if (HL_EXPECT_FALSE(ptr == nullptr)) {
	errno = ENOMEM;
      }
      return ptr;
    }
    // We still haven't initialized the heap. Satisfy this memory
    // request from the local buffer. Check remaining capacity BEFORE
    // advancing the pointer: computing initBufferPtr + sz first (as the old
    // code did) is pointer-arithmetic overflow for large sz, and the wrapped
    // result can compare below the end and slip past the bounds check. sz is
    // already capped at HOARD_MAX_ALLOCATION above, so the subtraction here
    // cannot underflow.
    void * ptr = initBufferPtr;
    if (sz > (size_t) ((initBuffer + MAX_LOCAL_BUFFER_SIZE) - initBufferPtr)) {
      errno = ENOMEM;
      return nullptr;
    }
    initBufferPtr += sz;
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

    // Over-aligned (alignment >= SUPERBLOCK_SIZE) allocations carry a stashed
    // canonical pointer; recover it. One superblock-alignment test on the hot
    // path (predicted-not-taken: ordinary pointers are never aligned here).
    if (HL_EXPECT_FALSE((reinterpret_cast<uintptr_t>(ptr) & (SUPERBLOCK_SIZE - 1)) == 0)) {
      void * canonical = hoardRecoverOverAligned (ptr);
      if (canonical != nullptr) {
        ptr = canonical;
      }
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
      // Guard the round-up: for sz within 15 of SIZE_MAX, (sz+15)&~15 wraps
      // to 0, after which the sz==0 nudge hands back a 16-byte object while
      // the caller believes it got an enormous one.
      if (sz > HOARD_MAX_ALLOCATION - 15) {
        return nullptr;
      }
      sz = (sz + 15) & ~(size_t) 15;
      if (sz == 0) {
        sz = 16;
      }
      return xxmalloc(sz);
    }

    // Over-allocate so an aligned address can be carved out. Requesting
    // sz + alignment guarantees the containing object is strictly larger than
    // `alignment`, which is what makes the two cases below correct. Guard the
    // addition against wraparound (and cap the total) first.
    if (sz > HOARD_MAX_ALLOCATION - alignment) {
      return nullptr;
    }
    size_t totalSize = sz + alignment;
    void* ptr = xxmalloc(totalSize);
    if (ptr == nullptr) {
      return nullptr;
    }

    // Calculate aligned address within the allocation.
    uintptr_t addr = reinterpret_cast<uintptr_t>(ptr);
    uintptr_t alignedAddr = (addr + alignment - 1) & ~(alignment - 1);
    void * aligned = reinterpret_cast<void*>(alignedAddr);

    if (alignment < SUPERBLOCK_SIZE) {
      // The aligned pointer stays inside the containing object -- the object
      // is larger than `alignment` (we asked for sz + alignment), so
      // alignedAddr < addr + alignment <= addr + objectSize -- and therefore
      // inside that object's FIRST superblock (alignment <= SUPERBLOCK_SIZE/2,
      // the largest power of two below SUPERBLOCK_SIZE, so the bump cannot
      // reach the next superblock boundary). Hoard's normalize()-based free
      // recovers the object base from it directly, and getSize() reports the
      // bytes remaining to the object end -- which is >= sz.
      return aligned;
    }

    // alignment >= SUPERBLOCK_SIZE: `aligned` is itself superblock-aligned, so
    // getSuperblock() cannot locate the header. Stash the canonical Hoard
    // pointer in the word just below `aligned`; xxfree / xxmalloc_usable_size
    // / xxrealloc recover it via hoardRecoverOverAligned(). There is always
    // room and the stash never touches the header: `ptr` is never superblock-
    // aligned, so alignedAddr - addr >= SUPERBLOCK_SIZE - sizeof(Header),
    // which is far larger than sizeof(void*).
    *reinterpret_cast<void **>(alignedAddr - sizeof(void *)) = ptr;
    return aligned;
  }

  HOARD_HOOK size_t xxmalloc_usable_size (void * ptr) {
    // Handle init buffer pointers
    if (ptr >= initBuffer && ptr < initBuffer + MAX_LOCAL_BUFFER_SIZE) {
      return static_cast<size_t>((initBuffer + MAX_LOCAL_BUFFER_SIZE) - (char*)ptr);
    }
    auto * heap = getCustomHeap();
    // Over-aligned allocation: report the bytes remaining from `ptr` to the
    // end of the underlying object (the object's usable size minus how far
    // `ptr` sits past the canonical pointer).
    if (HL_EXPECT_FALSE((reinterpret_cast<uintptr_t>(ptr) & (SUPERBLOCK_SIZE - 1)) == 0)) {
      void * canonical = hoardRecoverOverAligned (ptr);
      if (canonical != nullptr) {
        size_t full = (heap != nullptr) ? heap->getSize(canonical) : 0;
        size_t off = reinterpret_cast<uintptr_t>(ptr)
                   - reinterpret_cast<uintptr_t>(canonical);
        return (full > off) ? (full - off) : 0;
      }
    }
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
