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
 * @brief  Size class lookup for the TLAB fast path (sizes 1-1024).
 *
 * A thin binding of Hoard::sizeclasses (sizeclasses.h), which is also what
 * HL::bins<Header, 262144> (bins256k.h) binds. Both therefore index the same
 * classes by construction: the TLAB indexes its local bins with these classes
 * while the parent heap uses HL::bins, and the two must agree for sizes up to
 * LargestSmallObject. That agreement used to rest on keeping two
 * hand-maintained tables in sync; it is now a compile-time property.
 */

#ifndef HOARD_SIZECLASSLUT_H
#define HOARD_SIZECLASSLUT_H

#include <cstddef>
#include <cstdint>

#include "hoard/sizeclasses.h"

namespace Hoard {

  /// Fast size class lookup for small objects.
  /// IMPORTANT: only valid for sz <= sizeclasses::kLutMaxSize (1024);
  /// larger sizes need the full bins256k.h path.
  static inline int getSizeClassLUT(size_t sz) {
    return sizeclasses::kLut.v[(sz + 7) >> 3];
  }

  /// Class size lookup (inverse of size class).
  static inline size_t getClassSizeLUT(int sizeClass) {
    return sizeclasses::classSize(sizeClass);
  }

}

#endif // HOARD_SIZECLASSLUT_H
