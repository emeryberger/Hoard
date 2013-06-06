// -*- C++ -*-

#ifndef HOARD_GEOMETRIC_SIZECLASS_H
#define HOARD_GEOMETRIC_SIZECLASS_H

namespace Hoard {

  template <int MaxOverhead = 20,  // percent
	    int Alignment = 16>
  class GeometricSizeClass {
  public:

    static int size2class (const size_t sz) {
      // size = Alignment * (1 + MaxOverhead) ^ class
      // log(size) = log (Alignment * (1 + MaxOverHead) ^ class)
      //           = log (Alignment) + class * log (1 + MaxOverhead)
      // => class = (log(size) - log(Alignment)) / log (1 + MaxOverhead)
      return ceil((log (sz) - log (Alignment))
		  / log (1.0 + (float) MaxOverhead / 100.0));
    }

    static size_t class2size (const int cl) {
      return Alignment * floor (pow (1.0 + (float) MaxOverhead / 100.0, cl));
    }

  };

}

#endif

