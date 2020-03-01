// -*- C++ -*-

/*

  The Hoard Multiprocessor Memory Allocator
  www.hoard.org

  Author: Emery Berger, http://www.emeryberger.com
  Copyright (c) 1998-2020 Emery Berger

  See the LICENSE file at the top-level directory of this
  distribution and at http://github.com/emeryberger/Hoard.

*/

#ifndef HOARD_GEOMETRIC_SIZECLASS_H
#define HOARD_GEOMETRIC_SIZECLASS_H

#include <cmath>
#include <cstdlib>
#include <cassert>

#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wself-assign"
#endif

#include <heaplayers.h>

namespace Hoard {

  // Helper function to statically compute integer logarithms.
  template <size_t BaseNumerator,
	    size_t BaseDenominator,
	    size_t Value>
  class ilog;

  template <size_t BaseNumerator, size_t BaseDenominator>
  class ilog<BaseNumerator, BaseDenominator, 1> {
  public:
    enum { VALUE = 0 };
  };


  template <size_t BaseNumerator,
	    size_t BaseDenominator,
	    size_t Value>
  class ilog {
  public:
    enum { VALUE = 1 + ilog<BaseNumerator,
	   BaseDenominator,
	   (Value * BaseDenominator) / BaseNumerator>::VALUE };
  };

  /// @class GeometricSizeClass
  /// @brief Manages geometrically-increasing size classes.

  template <size_t MaxOverhead = 20,  // Percent internal fragmentation
	    size_t Alignment = 16>   // Minimum required alignment.
  class GeometricSizeClass {
  public:

    GeometricSizeClass()
    {
      assert (test());
    }

    /// Return the size class for a given size.
    static int constexpr size2class (const size_t sz) {
      // Do a binary search to find the right size class.
      int left  = 0;
      int right = NUM_SIZECLASSES - 1;
      while (left < right) {
	int mid = (left + right)/2;
	if (c2s(mid) < sz) {
	  left = mid + 1;
	} else {
	  right = mid;
	}
      }
      assert (c2s(left) >= sz);
      assert ((left == 0) || (c2s(left-1) < sz));
      return left;
    }

    /// Return the maximum size for a given size class.
    static size_t constexpr class2size (const int cl) {
      return c2s (cl);
    }

#if defined(__LP64__) || defined(_LP64) || defined(_WIN64) || defined(__x86_64__)
    // The maximum size of an object, in 64-bit land.
    enum { MaxObjectSize = (1UL << 31) };
#else
    // The maximum size of an object for 32-bit architectures.
    enum { MaxObjectSize = (1UL << 25) };
#endif

  private:

    /// Verify that this class is working properly.
    static bool constexpr test() {
      // Iterate just up to 1MB for now.
      for (size_t sz = Alignment; sz < 1048576; sz += Alignment) {
	int cl = size2class (sz);
	if (sz > class2size(cl)) {
	  assert (sz <= class2size(cl));
	  return false;
	}
      }
      for (int cl = 0; cl < NUM_SIZECLASSES; cl++) {
	size_t sz = class2size (cl);
	if (cl != size2class(sz)) {
	  assert (cl == size2class(sz));
	  return false;
	}
      }
      return true;
    }

    /// The total number of size classes.
    enum { NUM_SIZECLASSES = ilog<100+MaxOverhead,
	   100,
	   MaxObjectSize>::VALUE };

    /// Quickly compute the maximum size for a given size class.
    static unsigned long c2s (int cl) {
      static size_t sizes[NUM_SIZECLASSES];
      static bool init = createTable ((size_t *) sizes);
      init = init;
      return sizes[cl];
    }

    /// Builds an array to speed size computations, stored in sizes.
    static bool createTable (size_t * sizes)
    {
      const double base =
	(1.0 + (double) MaxOverhead / (double) 100.0);
      size_t sz = Alignment;
      for (int i = 0; i < NUM_SIZECLASSES; i++) {
	sizes[i] = sz;
	size_t newSz = (size_t) (floor ((double) base * (double) sz));
	newSz = newSz - (HL::Modulo<Alignment>::mod (newSz));
	while ((double) newSz / (double) sz < base) {
	  newSz += Alignment;
	}
	sz = newSz;
      }
      return true;
    }

  };

}

#if defined(__clang__)
#pragma clang diagnostic pop
#endif

#endif

