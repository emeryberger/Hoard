// -*- C++ -*-

/*

  The Hoard Multiprocessor Memory Allocator
  www.hoard.org

  Author: Emery Berger, http://www.emeryberger.com
  Copyright (c) 1998-2026 Emery Berger

  See the LICENSE file at the top-level directory of this
  distribution and at http://github.com/emeryberger/Hoard.

*/

#ifndef HL_BINS256K_H
#define HL_BINS256K_H

#include <cassert>
#include <cstddef>

#include "heaplayers.h"

namespace HL {

/// Specialized size classes for 256KB superblocks.
///
/// Replaces the generic power-of-two classes (worst-case internal
/// fragmentation ~100%) with finer classes, mirroring jemalloc's and
/// mimalloc's spacing:
///   8-128 bytes:    8-byte increments (16 classes)
///   128-1024 bytes: four classes per power of two (~25% max waste)
///   1024-32768:     four classes per power of two (~25% max waste)
///
/// Non-power-of-two object sizes are fully supported by the superblock
/// header via a precomputed multiplicative inverse (see fastModulo in
/// hoardsuperblockheader.h).
///
/// Uses a lookup table for sizes up to 1024 for O(1) size class
/// computation. This table must stay in sync with Hoard::sizeClassLUT
/// (sizeclasslut.h), which the TLAB uses on its fast path.
template <class Header>
class bins<Header, 262144> {
public:

  bins() {
    static_assert(BIG_OBJECT > 0, "BIG_OBJECT must be positive.");
    static_assert(getClassSize(NUM_BINS - 1) == BIG_OBJECT,
		  "Largest class must equal the big-object threshold.");
    static_assert(getSizeClass(BIG_OBJECT) == NUM_BINS - 1,
		  "Big-object threshold must map to the last class.");
    static_assert(getSizeClass(1) == 0, "Smallest size must map to class 0.");
    static_assert(getSizeClass(8) == 0, "8 must map to class 0 (8 bytes).");
    static_assert(getSizeClass(9) == 1, "9 must map to class 1 (16 bytes).");
    static_assert(getSizeClass(1025) == 28, "1025 must map to class 28 (1280).");
    static_assert(getSizeClass(2048) == 31, "2048 must map to class 31.");
    static_assert(getSizeClass(2049) == 32, "2049 must map to class 32 (2560).");
  }

  enum { NUM_BINS = 48 };
  enum { BIG_OBJECT = 262144 / 8 };
  enum { NumBins = NUM_BINS };
  enum { MaxObjectSize = BIG_OBJECT };

  // Lookup table size: 1024/8 = 128 entries covers all sizes up to 1024.
  enum { LOOKUP_TABLE_SIZE = 128 };

  static inline constexpr int getSizeClass(size_t sz) {
    // Fast path: sizes up to 1024 use direct table lookup.
    if (sz <= 1024) {
      // Round up to 8-byte boundary and index into table.
      size_t idx = (sz + 7) >> 3;
      if (idx == 0) idx = 1;
      return _lookup[idx - 1];
    }
    // Slow path: larger sizes computed from the quarter-spaced geometry.
    return getSizeClassLarge(sz);
  }

  static inline constexpr size_t getClassSize(int i) {
    assert(i >= 0 && i < NUM_BINS);
    return _sizes[i];
  }

  static inline constexpr size_t getClassMaxSize(int i) {
    return getClassSize(i);
  }

private:

  // constexpr integer log base two, for use in constant evaluation and
  // on the (rare) large-size path.
  static constexpr inline unsigned int ilog2c(const size_t n) {
    return ((n <= 1) ? 0 : 1 + ilog2c(n / 2));
  }

  /// Classes for 1024 < sz <= 32768: four per power of two.
  /// For sz in (2^k, 2^(k+1)], classes at 2^k + q * 2^(k-2), q = 1..4.
  static inline constexpr int getSizeClassLarge(size_t sz) {
    unsigned int k = ilog2c(sz - 1);            // 10..14
    size_t base = (size_t)1 << k;
    size_t quarter = base >> 2;
    int q = (int)((sz - base + quarter - 1) >> (k - 2)); // 1..4
    return 27 + (int)(k - 10) * 4 + q;
  }

  // Size class table:
  // 0-15:  Small (8-byte increments): 8,16,24,...,128
  // 16-27: Medium (quarter spacing): 160,192,224,256,320,384,448,512,640,768,896,1024
  // 28-47: Large (quarter spacing): 1280,...,32768
  static constexpr size_t _sizes[NUM_BINS] = {
    // Small: 8-byte increments (16 classes)
    8, 16, 24, 32, 40, 48, 56, 64, 72, 80, 88, 96, 104, 112, 120, 128,
    // Medium: quarter spacing (12 classes)
    160, 192, 224, 256, 320, 384, 448, 512, 640, 768, 896, 1024,
    // Large: quarter spacing (20 classes)
    1280, 1536, 1792, 2048,
    2560, 3072, 3584, 4096,
    5120, 6144, 7168, 8192,
    10240, 12288, 14336, 16384,
    20480, 24576, 28672, 32768
  };

  // Lookup table: maps (size+7)/8 - 1 to size class for sizes 1-1024.
  // Entry i corresponds to sizes (i*8+1) to ((i+1)*8).
  static constexpr int _lookup[LOOKUP_TABLE_SIZE] = {
    // Generated: entry i covers sizes (i*8+1)..((i+1)*8); value = size class.
    0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15,
    16, 16, 16, 16, 17, 17, 17, 17, 18, 18, 18, 18, 19, 19, 19, 19,
    20, 20, 20, 20, 20, 20, 20, 20, 21, 21, 21, 21, 21, 21, 21, 21,
    22, 22, 22, 22, 22, 22, 22, 22, 23, 23, 23, 23, 23, 23, 23, 23,
    24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24,
    25, 25, 25, 25, 25, 25, 25, 25, 25, 25, 25, 25, 25, 25, 25, 25,
    26, 26, 26, 26, 26, 26, 26, 26, 26, 26, 26, 26, 26, 26, 26, 26,
    27, 27, 27, 27, 27, 27, 27, 27, 27, 27, 27, 27, 27, 27, 27, 27
  };
};

template <class Header>
constexpr size_t bins<Header, 262144>::_sizes[NUM_BINS];

template <class Header>
constexpr int bins<Header, 262144>::_lookup[LOOKUP_TABLE_SIZE];

} // namespace HL

#endif // HL_BINS256K_H
