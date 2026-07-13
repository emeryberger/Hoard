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
 * @file   sizeclasses.h
 * @brief  The single source of truth for Hoard's 256KB-superblock size classes.
 *
 * Everything else is derived from kSizes at compile time:
 *   - bins256k.h  (HL::bins<Header, 262144>): used by the parent heap
 *   - sizeclasslut.h (Hoard::getSizeClassLUT): used by the TLAB fast path
 *
 * These two used to be hand-maintained tables that "MUST stay in sync".
 * They are now both generated here, and the consistency conditions are
 * static_asserted below, so a mismatch is a compile error rather than a
 * silent mis-binning (a TLAB bin holding objects smaller than the class
 * it is indexed by would hand out undersized memory).
 *
 * ALIGNMENT INVARIANT
 * -------------------
 * Every class is a multiple of kQuantum, and the smallest class IS
 * kQuantum. Objects are packed contiguously from a kQuantum-aligned
 * superblock base, so a class size that is not a multiple of kQuantum
 * misaligns every subsequent object in the superblock. kQuantum is 16 =
 * malloc's fundamental-alignment guarantee on 64-bit targets.
 *
 * Hoard historically used 8-byte-spaced classes here (8, 24, 40, ..., 120)
 * and consequently returned 8-byte-aligned pointers for half of all
 * allocations in those classes. hoardsuperblockheader.h asserts the
 * invariant; those asserts are checked here too, at compile time.
 *
 * Note on a possible 8-byte class: on some targets (arm64 macOS, 32-bit
 * ARM) alignof(max_align_t) is only 8, so an 8-byte class would be
 * conforming, and mimalloc/jemalloc ship one. Hoard deliberately does not:
 * as a drop-in replacement for a system malloc that has always returned
 * 16-byte-aligned memory, handing back 8-byte-aligned pointers would break
 * (non-conforming but real) code that tags the low bits of heap pointers.
 * If that is ever revisited, gate it on alignof(std::max_align_t) <= 8 --
 * NOT on the ISA: arm64 Linux has a 128-bit long double and needs 16.
 */

#ifndef HOARD_SIZECLASSES_H
#define HOARD_SIZECLASSES_H

#include <cstddef>
#include <cstdint>

namespace Hoard {
  namespace sizeclasses {

    // NOTE: the namespace-scope constants and tables below are declared
    // `static constexpr`, NOT `inline constexpr`. libhoard.cpp does
    // `#define inline __forceinline` on Windows, which MSVC rejects on data
    // declarations ("'__forceinline' not permitted on data declarations").
    // ownershipmap.h carries the same warning. Internal linkage per TU is
    // fine here: these are compile-time constants, and the tables are a few
    // hundred bytes.

    /// Alignment quantum: minimum class size AND class-size granularity.
    static constexpr size_t kQuantum = 16;

    /// Largest object served from a size class (the big-object threshold).
    static constexpr size_t kBigObject = 32768;

    /// Sizes up to this are resolved by lookup table; above, by closed form.
    static constexpr size_t kLutMaxSize = 1024;

    static constexpr int kMaxBins = 64;

    struct SizeTable {
      size_t sizes[kMaxBins] = {};
      int count = 0;
    };

    /// The size classes:
    ///   kQuantum..128     : kQuantum-byte steps
    ///   128..kBigObject   : four classes per power of two (<=25% waste)
    constexpr SizeTable makeSizes() {
      SizeTable t {};
      for (size_t s = kQuantum; s <= 128; s += kQuantum) {
        t.sizes[t.count++] = s;
      }
      // Four classes per power of two: for sz in (2^k, 2^(k+1)],
      // classes sit at 2^k + q * 2^(k-2), q = 1..4.
      for (size_t base = 128; base < kBigObject; base *= 2) {
        for (int q = 1; q <= 4; q++) {
          t.sizes[t.count++] = base + q * (base / 4);
        }
      }
      return t;
    }

    static constexpr SizeTable kTable = makeSizes();
    static constexpr int kNumBins = kTable.count;

    /// The class sizes, compacted to exactly kNumBins entries and put on a
    /// cache line: this is read on the TLAB malloc/free fast path (via
    /// getClassSizeLUT), so it should not straddle lines or drag in the
    /// unused tail of the staging table above.
    struct alignas(64) ClassSizes {
      size_t v[kNumBins] = {};
    };

    constexpr ClassSizes makeClassSizes() {
      ClassSizes cs {};
      for (int i = 0; i < kNumBins; i++) {
        cs.v[i] = kTable.sizes[i];
      }
      return cs;
    }

    static constexpr ClassSizes kSizes = makeClassSizes();

    constexpr size_t classSize(int c) {
      return kSizes.v[c];
    }

    /// Reference implementation: the smallest class that fits sz.
    /// Used to build the LUT and to validate the fast paths below.
    constexpr int classForReference(size_t sz) {
      for (int i = 0; i < kNumBins; i++) {
        if (sz <= kTable.sizes[i]) {
          return i;
        }
      }
      return -1;
    }

    /// Index of the last class at or below kLutMaxSize. The closed-form
    /// large path is anchored to this, so it cannot drift from the table.
    constexpr int lastLutClass() {
      return classForReference(kLutMaxSize);
    }

    constexpr unsigned int ilog2c(size_t n) {
      return (n <= 1) ? 0 : 1 + ilog2c(n / 2);
    }

    /// Closed form for kLutMaxSize < sz <= kBigObject (no table scan).
    constexpr int classForLarge(size_t sz) {
      unsigned int k = ilog2c(sz - 1);
      size_t base = (size_t) 1 << k;
      size_t quarter = base >> 2;
      int q = (int) ((sz - base + quarter - 1) >> (k - 2));   // 1..4
      return lastLutClass() + (int) (k - ilog2c(kLutMaxSize)) * 4 + q;
    }

    /// Lookup table for sz <= kLutMaxSize, at 8-byte granularity.
    /// Index = (sz + 7) / 8, so entry k covers sizes 8(k-1)+1 .. 8k.
    static constexpr int kLutEntries = (int) (kLutMaxSize / 8) + 1;   // 129

    /// Cache-line aligned: read on the TLAB malloc fast path.
    struct alignas(64) Lut {
      uint8_t v[kLutEntries] = {};
    };

    constexpr Lut makeLut() {
      Lut l {};
      l.v[0] = 0;                       // malloc(0) -> smallest class
      for (int k = 1; k < kLutEntries; k++) {
        l.v[k] = (uint8_t) classForReference((size_t) k * 8);
      }
      return l;
    }

    static constexpr Lut kLut = makeLut();

    /// The fast path used by both consumers.
    constexpr int classFor(size_t sz) {
      if (sz <= kLutMaxSize) {
        return kLut.v[(sz + 7) >> 3];
      }
      return classForLarge(sz);
    }

    // ---- Compile-time proof that everything above agrees ----------------

    /// Every class is a multiple of kQuantum, the smallest IS kQuantum, and
    /// the table is strictly increasing and ends at the big-object threshold.
    /// This is the invariant hoardsuperblockheader.h asserts at runtime.
    constexpr bool sizesWellFormed() {
      if (kTable.sizes[0] != kQuantum) {
        return false;
      }
      if (kTable.sizes[kNumBins - 1] != kBigObject) {
        return false;
      }
      for (int i = 0; i < kNumBins; i++) {
        if (kTable.sizes[i] % kQuantum != 0) {
          return false;
        }
        if (i > 0 && kTable.sizes[i] <= kTable.sizes[i - 1]) {
          return false;
        }
      }
      return true;
    }

    /// The fast path (LUT + closed form) must agree with the reference
    /// classifier, and each class must be big enough for every request
    /// mapped to it.
    ///
    /// Checking each class's boundary sizes covers every size in between:
    /// classFor is monotonic (below kLutMaxSize it is a table lookup whose
    /// buckets are built from classForReference, and class sizes are
    /// multiples of 16 so no bucket straddles a class; above, it is a
    /// ceiling division). A full 1..kBigObject sweep would exceed clang's
    /// default constexpr step budget.
    constexpr bool classifiersAgree() {
      for (int c = 0; c < kNumBins; c++) {
        size_t lo = (c == 0) ? 1 : classSize(c - 1) + 1;   // smallest size in class
        size_t hi = classSize(c);                          // largest size in class
        if (classFor(lo) != c || classFor(hi) != c) {
          return false;
        }
        if (classForReference(lo) != c || classForReference(hi) != c) {
          return false;
        }
        // The next size up must spill into the next class.
        if (c + 1 < kNumBins && classFor(hi + 1) != c + 1) {
          return false;
        }
      }
      // Every LUT bucket must map to a class that actually fits the
      // largest size in that bucket -- a bin indexed by a class whose
      // objects are smaller than the request would hand out undersized
      // memory. This is the exact hazard the old two-table setup risked.
      for (int k = 1; k < kLutEntries; k++) {
        size_t largest = (size_t) k * 8;
        int c = kLut.v[k];
        if (classSize(c) < largest) {
          return false;
        }
        if (c > 0 && classSize(c - 1) >= largest) {
          return false;   // not the *smallest* fitting class
        }
      }
      return true;
    }

    static_assert(kNumBins <= kMaxBins, "Size class table overflowed kMaxBins.");
    static_assert(sizesWellFormed(),
                  "Size classes must be increasing multiples of kQuantum, "
                  "starting at kQuantum and ending at kBigObject.");
    static_assert(classifiersAgree(),
                  "LUT / closed-form size class disagrees with the table.");

  }
}

#endif // HOARD_SIZECLASSES_H
