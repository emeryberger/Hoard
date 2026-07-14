// -*- C++ -*-

/*

  The Hoard Multiprocessor Memory Allocator
  www.hoard.org

  Author: Emery Berger, http://www.emeryberger.com
  Copyright (c) 1998-2026 Emery Berger

  See the LICENSE file at the top-level directory of this
  distribution and at http://github.com/emeryberger/Hoard.

*/

/**
 * @file ownershipmap.h
 * @brief O(1) ownership predicate over ChunkSize-aligned address chunks.
 *
 * Tracks which ChunkSize-aligned chunks of the address space belong to
 * Hoard. Registration happens at the mmap layer (alignedmmap.h), where
 * every region Hoard manages originates and where region sizes are
 * rounded to a ChunkSize multiple, so chunk ownership is exact: a chunk
 * is either entirely Hoard's or not Hoard's at all.
 *
 * This backs the xxowns() hook consumed by the alloc8 interposition
 * layer on macOS. With it, alloc8 skips its per-pointer size table
 * (a hash insert/erase on every malloc/free that saturates at ~4M live
 * objects), answering ownership with two loads instead.
 *
 * Memory: one bit per chunk over a 2^48-byte address space. At the
 * 256KB superblock size that is 128MB of *virtual* space, mapped lazily
 * and never touched except for pages covering address ranges Hoard
 * actually uses (one 16KB page of bitmap covers 32GB of address space).
 */

#ifndef HOARD_OWNERSHIPMAP_H
#define HOARD_OWNERSHIPMAP_H

#include <atomic>
#include <cstdint>
#include <cstddef>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace Hoard {

#if !defined(_WIN32)
  namespace ownershipdetail {
    // Fixed bitmap geometry: one bit per 256KB chunk over a 2^48-byte
    // address space. The storage lives in libhoard.cpp as an ordinary
    // (non-weak) zero-initialized global so it lands in a zerofill
    // segment — a weak definition here (e.g. a template static member)
    // would be coalesced into __data and bloat the binary by 128MB.
    // Hidden visibility keeps every reference within the dylib direct
    // (no GOT load on the free fast path).
    //
    // Plain uint64_t accessed through std::atomic_ref, NOT std::atomic:
    // an array of std::atomic cannot be constant-initialized with libc++,
    // so it would acquire a dynamic initializer that zeroes the whole map
    // from __mod_init_func — after Hoard has already started registering
    // superblocks. See the definition in libhoard.cpp. atomic_ref gives
    // the identical atomic operations on storage that is guaranteed to be
    // zerofill with no constructor.
    constexpr size_t kChunkSize = 262144;
    constexpr size_t kNumWords = (1ULL << 48) / kChunkSize / 64;

#if defined(__APPLE__)
    // Mach-O: declared here, DEFINED in libhoard.cpp (see the note above --
    // a weak/coalesced 128MB symbol cannot land in a zerofill section here).
    extern __attribute__((visibility("hidden")))
    uint64_t bits[kNumWords];
#else
    // ELF: define the storage right here as an inline variable. It is a
    // plain integer array with no initializer, so it lands in .bss (zerofill,
    // no file-size cost) and COMDAT-dedupes to one copy per shared object.
    //
    // This must NOT be confined to libhoard.cpp: alignedmmap.h calls
    // OwnershipMap::set() unconditionally, so ANY consumer that builds Hoard's
    // heaps from these headers without also compiling libhoard.cpp (alloc8's
    // examples/hoard does exactly that) would otherwise fail to link with
    // "undefined reference to Hoard::ownershipdetail::bits" -- and, because
    // the symbol is hidden, it cannot be satisfied from another shared object
    // either. Defining it in the header keeps such consumers working.
    inline __attribute__((visibility("hidden")))
    uint64_t bits[kNumWords];
#endif

    using AtomicWord = std::atomic_ref<uint64_t>;
    static_assert (alignof(uint64_t) >= AtomicWord::required_alignment,
                   "bitmap words must be suitably aligned for atomic_ref");
  }
#endif

  template <size_t ChunkSize>
  class OwnershipMap {
  public:

    static inline bool contains (const void * p) {
      auto a = reinterpret_cast<uintptr_t>(p);
      if (a >= MaxAddress) {
        return false;
      }
#if defined(_WIN32)
      auto * l1 = _l1.load (std::memory_order_acquire);
      if (l1 == nullptr) {
        return false;
      }
      auto * l2 = l1[a / L2Span].load (std::memory_order_acquire);
      if (l2 == nullptr) {
        return false;
      }
      size_t chunk = (a % L2Span) / ChunkSize;
      uint64_t word = l2[chunk >> 6].load (std::memory_order_relaxed);
      return (word >> (chunk & 63)) & 1;
#else
      // Relaxed load: the bit for a chunk is set (with release ordering)
      // before any pointer inside that chunk can escape a malloc, so any
      // execution in which a Hoard pointer legitimately reaches free()
      // already carries the happens-before edge that makes its bit
      // visible. One load and a bit test; the bitmap itself is static
      // storage, so there is no pointer chase and no init check.
      size_t chunk = a / ChunkSize;
      uint64_t word =
        ownershipdetail::AtomicWord (ownershipdetail::bits[chunk >> 6])
          .load (std::memory_order_relaxed);
      return (word >> (chunk & 63)) & 1;
#endif
    }

    // Register [start, start+len) as Hoard-owned. Called with len a
    // multiple of ChunkSize and start ChunkSize-aligned.
    static void set (void * start, size_t len) {
#if defined(_WIN32)
      forEachChunkAddr (start, len, [] (uintptr_t a) {
        auto * l2 = ensureL2 (a);
        if (l2 == nullptr) {
          return;
        }
        size_t chunk = (a % L2Span) / ChunkSize;
        l2[chunk >> 6].fetch_or (1ULL << (chunk & 63),
                                 std::memory_order_release);
      });
#else
      forEachChunk (start, len, [] (size_t chunk) {
        ownershipdetail::AtomicWord (ownershipdetail::bits[chunk >> 6])
          .fetch_or (1ULL << (chunk & 63), std::memory_order_release);
      });
#endif
    }

    static void clear (void * start, size_t len) {
#if defined(_WIN32)
      auto * l1 = _l1.load (std::memory_order_acquire);
      if (l1 == nullptr) {
        return;
      }
      forEachChunkAddr (start, len, [l1] (uintptr_t a) {
        auto * l2 = l1[a / L2Span].load (std::memory_order_acquire);
        if (l2 == nullptr) {
          return;
        }
        size_t chunk = (a % L2Span) / ChunkSize;
        l2[chunk >> 6].fetch_and (~(1ULL << (chunk & 63)),
                                  std::memory_order_release);
      });
#else
      forEachChunk (start, len, [] (size_t chunk) {
        ownershipdetail::AtomicWord (ownershipdetail::bits[chunk >> 6])
          .fetch_and (~(1ULL << (chunk & 63)), std::memory_order_release);
      });
#endif
    }

  private:

    static constexpr uintptr_t MaxAddress = 1ULL << 48;
    static constexpr size_t NumChunks = MaxAddress / ChunkSize;
    static constexpr size_t NumWords = NumChunks / 64;

#if defined(_WIN32)
    // Windows has no overcommit: a flat 128MB bitmap would charge the
    // pagefile up front. Use a two-level radix instead: a small L1
    // pointer array (committed once) whose entries each cover 64GB of
    // address space via an on-demand-committed L2 bitmap.
    static constexpr uintptr_t L2Span = 1ULL << 36; // 64GB per L2 node
    static constexpr size_t L1Entries = MaxAddress / L2Span; // 4096
    static constexpr size_t L2Words = (L2Span / ChunkSize) / 64;
#endif

    template <class F>
    static void forEachChunk (void * start, size_t len, F f) {
      auto a = reinterpret_cast<uintptr_t>(start);
      if (a >= MaxAddress) {
        return;
      }
      size_t first = a / ChunkSize;
      size_t last = (a + len + ChunkSize - 1) / ChunkSize; // exclusive
      for (size_t c = first; c < last && c < NumChunks; c++) {
        f (c);
      }
    }

#if defined(_WIN32)
    /// Like forEachChunk but passes the chunk's base address.
    template <class F>
    static void forEachChunkAddr (void * start, size_t len, F f) {
      auto a = reinterpret_cast<uintptr_t>(start);
      if (a >= MaxAddress) {
        return;
      }
      uintptr_t first = (a / ChunkSize) * ChunkSize;
      uintptr_t lastEx = ((a + len + ChunkSize - 1) / ChunkSize) * ChunkSize;
      for (uintptr_t c = first; c < lastEx && c < MaxAddress; c += ChunkSize) {
        f (c);
      }
    }

    static std::atomic<uint64_t> * ensureL2 (uintptr_t a) {
      auto * l1 = _l1.load (std::memory_order_acquire);
      if (l1 == nullptr) {
        void * mem = VirtualAlloc (nullptr,
                                   L1Entries * sizeof(void *),
                                   MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
        if (mem == nullptr) {
          return nullptr;
        }
        auto * fresh =
          reinterpret_cast<std::atomic<std::atomic<uint64_t> *> *>(mem);
        std::atomic<std::atomic<uint64_t> *> * expected = nullptr;
        if (!_l1.compare_exchange_strong (expected, fresh,
                                          std::memory_order_acq_rel)) {
          VirtualFree (mem, 0, MEM_RELEASE);
          l1 = expected;
        } else {
          l1 = fresh;
        }
      }
      auto & slot = l1[a / L2Span];
      auto * l2 = slot.load (std::memory_order_acquire);
      if (l2 != nullptr) {
        return l2;
      }
      void * mem = VirtualAlloc (nullptr, L2Words * sizeof(uint64_t),
                                 MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
      if (mem == nullptr) {
        return nullptr;
      }
      auto * fresh = reinterpret_cast<std::atomic<uint64_t> *>(mem);
      std::atomic<uint64_t> * expected = nullptr;
      if (!slot.compare_exchange_strong (expected, fresh,
                                         std::memory_order_acq_rel)) {
        VirtualFree (mem, 0, MEM_RELEASE);
        return expected;
      }
      return fresh;
    }
#endif

#if defined(_WIN32)
    // Defined out of line below. NOTE: deliberately not an inline
    // static member: libhoard.cpp does `#define inline __forceinline`
    // on Windows, which is invalid on data declarations; a template's
    // static member may be defined in a header without `inline`.
    static std::atomic<std::atomic<std::atomic<uint64_t> *> *> _l1;
#else
    // Unix storage is the fixed-geometry zerofill array declared in
    // ownershipdetail above (defined in libhoard.cpp).
    static_assert (ChunkSize == ownershipdetail::kChunkSize,
                   "OwnershipMap chunk size must match the static bitmap");
    static_assert (NumWords == ownershipdetail::kNumWords,
                   "OwnershipMap bitmap geometry mismatch");
#endif
  };

#if defined(_WIN32)
  template <size_t ChunkSize>
  std::atomic<std::atomic<std::atomic<uint64_t> *> *> OwnershipMap<ChunkSize>::_l1 { nullptr };
#endif

}

#endif // HOARD_OWNERSHIPMAP_H
