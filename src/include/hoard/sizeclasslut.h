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
 *   16-128 bytes:   16-byte increments (classes 0-7)
 *   128-1024 bytes: four classes per power of two (classes 8-19)
 *
 * Every class is a multiple of 16 so that contiguously packed objects
 * stay 16-byte aligned (malloc's fundamental-alignment guarantee).
 * See the alignment note in bins256k.h.
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
    0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7,
    7, 8, 8, 8, 8, 9, 9, 9, 9, 10, 10, 10, 10, 11, 11, 11,
    11, 12, 12, 12, 12, 12, 12, 12, 12, 13, 13, 13, 13, 13, 13, 13,
    13, 14, 14, 14, 14, 14, 14, 14, 14, 15, 15, 15, 15, 15, 15, 15,
    15, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16,
    16, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17,
    17, 18, 18, 18, 18, 18, 18, 18, 18, 18, 18, 18, 18, 18, 18, 18,
    18, 19, 19, 19, 19, 19, 19, 19, 19, 19, 19, 19, 19, 19, 19, 19,
    19
  };

  // Fast size class lookup for small objects (≤ 1024 bytes).
  // IMPORTANT: Only use for sizes <= 1024; larger sizes need the
  // full bins256k.h path.
  static inline int getSizeClassLUT(size_t sz) {
    // Round up to 8-byte granularity and lookup
    size_t index = (sz + 7) >> 3;
    return sizeClassLUT[index];
  }

  // Class size lookup (inverse of size class) for classes 0-19.
  // Returns the actual allocation size for a given size class.
  alignas(64) static constexpr size_t classSizeLUT[20] = {
    16, 32, 48, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320, 384, 448, 512, 640, 768, 896, 1024
  };

  static inline size_t getClassSizeLUT(int sizeClass) {
    return classSizeLUT[sizeClass];
  }

}

#endif // HOARD_SIZECLASSLUT_H
