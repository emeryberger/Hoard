// -*- C++ -*-

/*

  The Hoard Multiprocessor Memory Allocator
  www.hoard.org

  Author: Emery Berger, http://www.emeryberger.com
  Copyright (c) 1998-2020 Emery Berger

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
/// Uses finer granularity for small objects to reduce internal fragmentation.
/// Size classes:
///   0-128 bytes:   8-byte increments (16 classes: 16,24,32,...,128)
///   128-1024:      ~20% spacing (classes: 160,192,224,256,320,384,448,512,640,768,896,1024)
///   1024+:         power-of-two (2048,4096,8192,16384,32768)
///
/// Uses a lookup table for sizes up to 1024 for O(1) size class computation.
template <class Header>
class bins<Header, 262144> {
public:

  bins() {
    static_assert(BIG_OBJECT > 0, "BIG_OBJECT must be positive.");
  }

  enum { NUM_BINS = 33 };
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
    // Slow path: large sizes use log2.
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

  static inline constexpr int getSizeClassLarge(size_t sz) {
    // Power-of-two for 2048, 4096, 8192, 16384, 32768
    // Use ilog2 for efficient computation.
    unsigned int log = HL::ilog2(sz);
    // 2048 = 2^11 -> class 27, 4096 = 2^12 -> class 28, etc.
    return (int)log - 11 + 27;
  }

  // Size class table:
  // 0-14:  Small (8-byte increments): 16,24,32,40,48,56,64,72,80,88,96,104,112,120,128
  // 15-26: Medium (~20% spacing): 160,192,224,256,320,384,448,512,640,768,896,1024
  // 27-32: Large (power-of-two): 2048,4096,8192,16384,32768,BIG_OBJECT
  static constexpr size_t _sizes[NUM_BINS] = {
    // Small: 8-byte increments (15 classes)
    16, 24, 32, 40, 48, 56, 64, 72, 80, 88, 96, 104, 112, 120, 128,
    // Medium: ~20% spacing (12 classes)
    160, 192, 224, 256, 320, 384, 448, 512, 640, 768, 896, 1024,
    // Large: power-of-two (6 classes)
    2048, 4096, 8192, 16384, 32768, BIG_OBJECT
  };

  // Lookup table: maps (size+7)/8 to size class for sizes 1-1024.
  // Index i corresponds to sizes (i*8+1) to ((i+1)*8).
  static constexpr int _lookup[LOOKUP_TABLE_SIZE] = {
    // 1-8->16, 9-16->16
    0, 0,
    // 17-24->24, 25-32->32
    1, 2,
    // 33-40->40, 41-48->48, 49-56->56, 57-64->64
    3, 4, 5, 6,
    // 65-72->72, 73-80->80, 81-88->88, 89-96->96
    7, 8, 9, 10,
    // 97-104->104, 105-112->112, 113-120->120, 121-128->128
    11, 12, 13, 14,
    // 129-136->160, 137-144->160, 145-152->160, 153-160->160
    15, 15, 15, 15,
    // 161-168->192, 169-176->192, 177-184->192, 185-192->192
    16, 16, 16, 16,
    // 193-200->224, 201-208->224, 209-216->224, 217-224->224
    17, 17, 17, 17,
    // 225-232->256, 233-240->256, 241-248->256, 249-256->256
    18, 18, 18, 18,
    // 257-264->320, ...320
    19, 19, 19, 19, 19, 19, 19, 19,
    // 321-384->384
    20, 20, 20, 20, 20, 20, 20, 20,
    // 385-448->448
    21, 21, 21, 21, 21, 21, 21, 21,
    // 449-512->512
    22, 22, 22, 22, 22, 22, 22, 22,
    // 513-640->640 (16 entries)
    23, 23, 23, 23, 23, 23, 23, 23, 23, 23, 23, 23, 23, 23, 23, 23,
    // 641-768->768 (16 entries)
    24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24,
    // 769-896->896 (16 entries)
    25, 25, 25, 25, 25, 25, 25, 25, 25, 25, 25, 25, 25, 25, 25, 25,
    // 897-1024->1024 (16 entries)
    26, 26, 26, 26, 26, 26, 26, 26, 26, 26, 26, 26, 26, 26, 26, 26
  };
};

template <class Header>
constexpr size_t bins<Header, 262144>::_sizes[NUM_BINS];

template <class Header>
constexpr int bins<Header, 262144>::_lookup[LOOKUP_TABLE_SIZE];

} // namespace HL

#endif // HL_BINS256K_H
