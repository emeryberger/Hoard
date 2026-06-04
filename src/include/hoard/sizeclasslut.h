// -*- C++ -*-

/*

  The Hoard Multiprocessor Memory Allocator
  www.hoard.org

  Author: Emery Berger, http://www.emeryberger.com
  Copyright (c) 1998-2020 Emery Berger

  See the LICENSE file at the top-level directory of this
  distribution and at http://github.com/emeryberger/Hoard.

*/

/**
 * @file   sizeclasslut.h
 * @brief  Lookup table for fast size class computation on small objects.
 *
 * This replaces the bsr-based log2 calculation with a simple table lookup
 * for sizes up to 1024 bytes. The table uses 8-byte granularity, requiring
 * only 128 bytes of memory.
 *
 * Performance: table lookup (~3 cycles) vs bsr (~5 cycles + dependency chain)
 *
 * Size classes (alignof(max_align_t) = 8 on most 64-bit systems):
 *   0: 1-8 bytes     -> class size 8
 *   1: 9-16 bytes    -> class size 16
 *   2: 17-32 bytes   -> class size 32
 *   3: 33-64 bytes   -> class size 64
 *   4: 65-128 bytes  -> class size 128
 *   5: 129-256 bytes -> class size 256
 *   6: 257-512 bytes -> class size 512
 *   7: 513-1024 bytes -> class size 1024
 */

#ifndef HOARD_SIZECLASSLUT_H
#define HOARD_SIZECLASSLUT_H

#include <cstddef>
#include <cstdint>

namespace Hoard {

  // Size class lookup table for sizes 1-1024 bytes.
  // Index = (size + 7) / 8, giving 8-byte granularity (0-128).
  // Value = size class (0-7 for power-of-2 classes: 8, 16, 32, 64, 128, 256, 512, 1024)
  //
  // Generated from: sizeClass = ilog2(max(sz, 8)) - 3
  alignas(64) static constexpr uint8_t sizeClassLUT[129] = {
    // Index 0-1: sizes 0-8 -> class 0 (8 bytes)
    0, 0,
    // Index 2: sizes 9-16 -> class 1 (16 bytes)
    1,
    // Index 3-4: sizes 17-32 -> class 2 (32 bytes)
    2, 2,
    // Index 5-8: sizes 33-64 -> class 3 (64 bytes)
    3, 3, 3, 3,
    // Index 9-16: sizes 65-128 -> class 4 (128 bytes)
    4, 4, 4, 4, 4, 4, 4, 4,
    // Index 17-32: sizes 129-256 -> class 5 (256 bytes)
    5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5,
    // Index 33-64: sizes 257-512 -> class 6 (512 bytes)
    6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6,
    6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6,
    // Index 65-128: sizes 513-1024 -> class 7 (1024 bytes)
    7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
    7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
    7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
    7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
  };

  // Fast size class lookup for small objects (≤ 1024 bytes).
  // Uses table lookup instead of bsr instruction.
  // IMPORTANT: Only use for sizes <= 1024; larger sizes need the full ilog2 path.
  static inline int getSizeClassLUT(size_t sz) {
    // Round up to 8-byte granularity and lookup
    size_t index = (sz + 7) >> 3;
    return sizeClassLUT[index];
  }

  // Class size lookup (inverse of size class)
  // Returns the actual allocation size for a given size class.
  alignas(64) static constexpr size_t classSizeLUT[8] = {
    8, 16, 32, 64, 128, 256, 512, 1024
  };

  static inline size_t getClassSizeLUT(int sizeClass) {
    return classSizeLUT[sizeClass];
  }

}

#endif // HOARD_SIZECLASSLUT_H
