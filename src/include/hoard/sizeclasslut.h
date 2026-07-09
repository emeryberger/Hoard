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
 * @file   sizeclasslut.h
 * @brief  Lookup table for fast size class computation on small objects.
 *
 * Used by the TLAB fast path for sizes 1-1024 (LargestSmallObject).
 * The table uses 8-byte granularity, requiring only 129 bytes.
 *
 * IMPORTANT: this mapping MUST stay in sync with the size classes in
 * bins256k.h (HL::bins<Header, 262144>): the TLAB indexes its local
 * bins with these classes while the parent heap uses HL::bins, and the
 * two must agree for sizes up to LargestSmallObject.
 *
 * Size classes (see bins256k.h):
 *   8-128 bytes:    8-byte increments (classes 0-15)
 *   128-1024 bytes: four classes per power of two (classes 16-27)
 */

#ifndef HOARD_SIZECLASSLUT_H
#define HOARD_SIZECLASSLUT_H

#include <cstddef>
#include <cstdint>

namespace Hoard {

  // Size class lookup table for sizes 0-1024 bytes.
  // Index = (size + 7) / 8, giving 8-byte granularity (0-128).
  // Value = size class in the bins256k.h table.
  alignas(64) static constexpr uint8_t sizeClassLUT[129] = {
    // Generated: index k = (sz+7)/8 covers sizes 8(k-1)+1..8k; value = class.
    0, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14,
    15, 16, 16, 16, 16, 17, 17, 17, 17, 18, 18, 18, 18, 19, 19, 19,
    19, 20, 20, 20, 20, 20, 20, 20, 20, 21, 21, 21, 21, 21, 21, 21,
    21, 22, 22, 22, 22, 22, 22, 22, 22, 23, 23, 23, 23, 23, 23, 23,
    23, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24,
    24, 25, 25, 25, 25, 25, 25, 25, 25, 25, 25, 25, 25, 25, 25, 25,
    25, 26, 26, 26, 26, 26, 26, 26, 26, 26, 26, 26, 26, 26, 26, 26,
    26, 27, 27, 27, 27, 27, 27, 27, 27, 27, 27, 27, 27, 27, 27, 27,
    27,
  };

  // Fast size class lookup for small objects (≤ 1024 bytes).
  // IMPORTANT: Only use for sizes <= 1024; larger sizes need the
  // full bins256k.h path.
  static inline int getSizeClassLUT(size_t sz) {
    // Round up to 8-byte granularity and lookup
    size_t index = (sz + 7) >> 3;
    return sizeClassLUT[index];
  }

  // Class size lookup (inverse of size class) for classes 0-26.
  // Returns the actual allocation size for a given size class.
  alignas(64) static constexpr size_t classSizeLUT[28] = {
    8, 16, 24, 32, 40, 48, 56, 64, 72, 80, 88, 96, 104, 112, 120, 128,
    160, 192, 224, 256, 320, 384, 448, 512, 640, 768, 896, 1024
  };

  static inline size_t getClassSizeLUT(int sizeClass) {
    return classSizeLUT[sizeClass];
  }

}

#endif // HOARD_SIZECLASSLUT_H
