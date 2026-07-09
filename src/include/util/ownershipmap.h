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

#if !defined(_WIN32)
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
      auto a = reinterpret_cast<uintptr_t>(p);
      if (a >= MaxAddress) {
        return false;
      }
      size_t chunk = a / ChunkSize;
      uint64_t word = bits[chunk >> 6].load (std::memory_order_relaxed);
      return (word >> (chunk & 63)) & 1;
    }

    // Register [start, start+len) as Hoard-owned. Called with len a
    // multiple of ChunkSize and start ChunkSize-aligned.
    static void set (void * start, size_t len) {
      auto * bits = ensureBits();
      if (bits == nullptr) {
        return;
      }
      forEachChunk (start, len, [bits] (size_t chunk) {
        bits[chunk >> 6].fetch_or (1ULL << (chunk & 63),
                                   std::memory_order_release);
      });
    }

    static void clear (void * start, size_t len) {
      auto * bits = _bits.load (std::memory_order_acquire);
      if (bits == nullptr) {
        return;
      }
      forEachChunk (start, len, [bits] (size_t chunk) {
        bits[chunk >> 6].fetch_and (~(1ULL << (chunk & 63)),
                                    std::memory_order_release);
      });
    }

  private:

    static constexpr uintptr_t MaxAddress = 1ULL << 48;
    static constexpr size_t NumChunks = MaxAddress / ChunkSize;
    static constexpr size_t NumWords = NumChunks / 64;

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

    static std::atomic<uint64_t> * ensureBits() {
      auto * bits = _bits.load (std::memory_order_acquire);
      if (bits != nullptr) {
        return bits;
      }
#if !defined(_WIN32)
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
#else
      return nullptr;
#endif
    }

    // Defined out of line below. NOTE: deliberately not an inline
    // static member: libhoard.cpp does `#define inline __forceinline`
    // on Windows, which is invalid on data declarations; a template's
    // static member may be defined in a header without `inline`.
    static std::atomic<std::atomic<uint64_t> *> _bits;
  };

  template <size_t ChunkSize>
  std::atomic<std::atomic<uint64_t> *> OwnershipMap<ChunkSize>::_bits { nullptr };

}

#endif // HOARD_OWNERSHIPMAP_H
