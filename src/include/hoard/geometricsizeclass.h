// -*- C++ -*-

#ifndef HOARD_GEOMETRIC_SIZECLASS_H
#define HOARD_GEOMETRIC_SIZECLASS_H

#include <cmath>
#include <cstdlib>
#include <cassert>

#include <iostream>


namespace Hoard {

  template <int MaxOverhead = 20,  // percent
	    int Alignment = 16>
  class GeometricSizeClass {
  public:

    GeometricSizeClass()
    {
      assert (test());
    }

    static int size2class (const size_t sz) {
      //      int index = 0;
      int index = (int) (floor(log(sz) - log(Alignment))
			 / ceil(log(1.0 + (double) MaxOverhead / 100.0)));
      //      int index = 0;
      while (sz > c2s(index)) {
	index++;
      }
      return index;
      // size = Alignment * (1 + MaxOverhead) ^ class
      // log(size) = log (Alignment * (1 + MaxOverHead) ^ class)
      //           = log (Alignment) + class * log (1 + MaxOverhead)
      // => class = (log(size) - log(Alignment)) / log (1 + MaxOverhead)
      //      return floor((log (sz) - log (Alignment))
      //		  / log (1.0 + (float) MaxOverhead / 100.0));
    }

    static size_t class2size (const int cl) {
      return c2s (cl);
      //      return Alignment * floor (pow (1.0 + (float) MaxOverhead / 100.0, cl));
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
    enum { NUM_SIZECLASSES = 80 }; // EDB: Magic number for now; FIX ME.

    static unsigned long c2s (int cl) {
      static size_t sizes[NUM_SIZECLASSES];
      static bool init = createTable (sizes);
      init = init;
      return sizes[cl];
    }

    static bool createTable (unsigned long * sizes)
    {
      const double base = (1.0 + (double) MaxOverhead / 100.0);
      size_t sz = Alignment;
      for (int i = 0; i < NUM_SIZECLASSES; i++) {
	sizes[i] = sz;
	size_t newSz = sz;
	newSz = (size_t) (floor ((double) base * (double) sz));
	newSz = newSz - (newSz % Alignment);
	while ((double) newSz / (double) sz < base) {
	  newSz += Alignment;
	}
	sz = newSz;
      }
      return true;
    }

  };

}

#endif

