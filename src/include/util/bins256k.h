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
#include "hoard/sizeclasses.h"

namespace HL {

/// Size classes for 256KB superblocks (macOS/Linux), as used by the parent
/// heap. Replaces the generic power-of-two classes (worst-case internal
/// fragmentation ~100%) with finer classes (<=25%).
///
/// This is a thin binding of Hoard::sizeclasses (sizeclasses.h) to the
/// Heap-Layers bins interface. The class table, the lookup table, and the
/// alignment invariant all live there; do not restate them here. The TLAB
/// fast path (sizeclasslut.h) binds the SAME table, which is what keeps the
/// two in agreement: they are no longer separate hand-maintained tables that
/// must be kept in sync by hand.
template <class Header>
class bins<Header, 262144> {
public:

  enum { NUM_BINS = Hoard::sizeclasses::kNumBins };
  enum { BIG_OBJECT = Hoard::sizeclasses::kBigObject };
  enum { NumBins = NUM_BINS };
  enum { MaxObjectSize = BIG_OBJECT };

  static inline constexpr int getSizeClass(size_t sz) {
    return Hoard::sizeclasses::classFor(sz);
  }

  static inline constexpr size_t getClassSize(int i) {
    assert(i >= 0 && i < NUM_BINS);
    return Hoard::sizeclasses::classSize(i);
  }

  static inline constexpr size_t getClassMaxSize(int i) {
    return getClassSize(i);
  }

  static_assert(getClassSize(NUM_BINS - 1) == BIG_OBJECT,
                "Largest class must equal the big-object threshold.");
  static_assert(getSizeClass(BIG_OBJECT) == NUM_BINS - 1,
                "Big-object threshold must map to the last class.");
  static_assert(getSizeClass(1) == 0,
                "Smallest size must map to class 0.");
};

} // namespace HL

#endif // HL_BINS256K_H
