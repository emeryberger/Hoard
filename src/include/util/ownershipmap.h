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
#else
#include <sys/mman.h>
#ifndef MAP_NORESERVE
#define MAP_NORESERVE 0
#endif
#endif

namespace Hoard {

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
      // Relaxed load on the hot path: the pointer is written exactly
      // once (before any Hoard-owned pointer can be freed), and the
      // subsequent array access carries an address dependency. Fall
      // back to an acquire load before concluding the map is absent.
      auto * bits = _bits.load (std::memory_order_relaxed);
#if defined(__GNUC__) || defined(__clang__)
      if (__builtin_expect(bits == nullptr, 0)) {
#else
      if (bits == nullptr) {
#endif
        bits = _bits.load (std::memory_order_acquire);
        if (bits == nullptr) {
          return false;
        }
      }
      size_t chunk = a / ChunkSize;
      uint64_t word = bits[chunk >> 6].load (std::memory_order_relaxed);
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
      auto * bits = ensureBits();
      if (bits == nullptr) {
        return;
      }
      forEachChunk (start, len, [bits] (size_t chunk) {
        bits[chunk >> 6].fetch_or (1ULL << (chunk & 63),
                                   std::memory_order_release);
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
      auto * bits = _bits.load (std::memory_order_acquire);
      if (bits == nullptr) {
        return;
      }
      forEachChunk (start, len, [bits] (size_t chunk) {
        bits[chunk >> 6].fetch_and (~(1ULL << (chunk & 63)),
                                    std::memory_order_release);
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

#if !defined(_WIN32)
    static std::atomic<uint64_t> * ensureBits() {
      auto * bits = _bits.load (std::memory_order_acquire);
      if (bits != nullptr) {
        return bits;
      }
      void * p = mmap (nullptr, NumWords * sizeof(uint64_t),
                       PROT_READ | PROT_WRITE,
                       MAP_PRIVATE | MAP_ANON | MAP_NORESERVE, -1, 0);
      if (p == MAP_FAILED) {
        return nullptr;
      }
      auto * fresh = reinterpret_cast<std::atomic<uint64_t> *>(p);
      std::atomic<uint64_t> * expected = nullptr;
      if (!_bits.compare_exchange_strong (expected, fresh,
                                          std::memory_order_acq_rel)) {
        munmap (p, NumWords * sizeof(uint64_t));
        return expected;
      }
      return fresh;
    }
#endif

    // Defined out of line below. NOTE: deliberately not inline
    // static members: libhoard.cpp does `#define inline __forceinline`
    // on Windows, which is invalid on data declarations; a template's
    // static member may be defined in a header without `inline`.
#if defined(_WIN32)
    static std::atomic<std::atomic<std::atomic<uint64_t> *> *> _l1;
#else
    static std::atomic<std::atomic<uint64_t> *> _bits;
#endif
  };

#if defined(_WIN32)
  template <size_t ChunkSize>
  std::atomic<std::atomic<std::atomic<uint64_t> *> *> OwnershipMap<ChunkSize>::_l1 { nullptr };
#else
  template <size_t ChunkSize>
  std::atomic<std::atomic<uint64_t> *> OwnershipMap<ChunkSize>::_bits { nullptr };
#endif

}

#endif // HOARD_OWNERSHIPMAP_H
