// -*- C++ -*-

#ifndef HOARD_GEOMETRIC_SIZECLASS_H
#define HOARD_GEOMETRIC_SIZECLASS_H

namespace Hoard {

  template <int MaxOverhead = 20,  // percent
	    int Alignment = 16>
  class GeometricSizeClass {
  public:

    static int size2class (const size_t sz) {
      static size_t sizes[NUM_SIZES];
      static bool init = createTable (sizes);
      int index = 0;
      while (sz > sizes[index]) {
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
      static size_t sizes[NUM_SIZES];
      static bool init = createTable (sizes);
      return sizes[cl];
      //      return Alignment * floor (pow (1.0 + (float) MaxOverhead / 100.0, cl));
    }

    static void test() {
      for (size_t sz = Alignment; sz < 1048576; sz += Alignment) {
	int cl = size2class (sz);
	assert (sz <= class2size(cl));
      }
      for (int cl = 0; cl < NUM_SIZES; cl++) {
	size_t sz = class2size (cl);
	assert (cl == size2class (sz));
      }
    }

  private:

    enum { NUM_SIZES = 80 };

    static bool createTable (unsigned long * sizes)
    {
      const float base = (1.0 + (float) MaxOverhead / 100.0);
      size_t sz = Alignment;
      for (int i = 0; i < NUM_SIZES; i++) {
	sizes[i] = sz;
	size_t newSz = sz;
	while ((log(newSz) - log(sz)) / log(base) < 1.0) {
	  newSz += Alignment;
	}
	sz = newSz;
      }
      return true;
    }

  };

}

#endif

