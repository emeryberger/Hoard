// -*- C++ -*-

#ifndef HOARD_GEOMETRIC_SIZECLASS_H
#define HOARD_GEOMETRIC_SIZECLASS_H

#include <cmath>
#include <cstdlib>
#include <cassert>

#include <iostream>

#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wself-assign"
#endif

namespace Hoard {

  template <int Base, int Value>
  class ilog;

  template <int Base>
  class ilog<Base, 1> {
  public:
    enum { VALUE = 0 };
  };

  template <int Base, int Value>
  class ilog {
  public:
    enum { VALUE = 1 + ilog<Base, (Value * 100) / Base>::VALUE };
  };

  template <int MaxOverhead = 20,  // percent
	    int Alignment = 16>
  class GeometricSizeClass {
  public:

    GeometricSizeClass()
    {
      assert (test());
    }

    static int size2class (const size_t sz) {
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

    static size_t class2size (const int cl) {
      return c2s (cl);
    }

    static bool test() {
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

  private:

    /// The total number of size classes.
    enum { NUM_SIZECLASSES = ilog<100+MaxOverhead, 1 << 24>::VALUE };

    static unsigned long c2s (int cl) {
      static size_t sizes[NUM_SIZECLASSES];
      static bool init = createTable ((size_t *) sizes);
      init = init;
      return sizes[cl];
    }

    static bool createTable (size_t * sizes)
    {
      const double base = (1.0 + (double) MaxOverhead / 100.0);
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

