// -*- C++ -*-

/*

  Heap Layers: An Extensible Memory Allocation Infrastructure
  
  Copyright (C) 2000-2012 by Emery Berger
  http://www.cs.umass.edu/~emery
  emery@cs.umass.edu
  
  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
  (at your option) any later version.
  
  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.
  
  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA

*/

#if !defined(HL_BINS64K_H)
#define HL_BINS64K_H

#include <cstdlib>
#include <assert.h>

#include "bins.h"
#include "sassert.h"

namespace HL {

  template <class Header>
  class bins<Header, 65536> {
  public:

    bins (void)
    {
#ifndef NDEBUG
      for (int i = sizeof(double); i < BIG_OBJECT; i++) {
	int sc = getSizeClass(i);
	assert (getClassSize(sc) >= i);
	assert (getClassSize(sc-1) < i);
	assert (getSizeClass(getClassSize(sc)) == sc);
      }
#endif
    }

    enum { BIG_OBJECT = 8192 };
    enum { NUM_BINS = 11 };

    static inline unsigned int bitScanReverse (size_t i) {
#ifdef __GNUC__
      return (sizeof(unsigned long) * 8) - __builtin_clzl(i) - 1;
#elif defined _WIN32
      unsigned long index;
      _BitScanReverse (&index, i);
      return index;
#else

#if 1
      // From http://graphics.stanford.edu/~seander/bithacks.html#ZerosOnRightMultLookup
      // Works for up to 32 bit sizes.
      int r;
      size_t v = i;

      static const int MultiplyDeBruijnBitPosition[32] = 
	{
	  0, 9, 1, 10, 13, 21, 2, 29, 11, 14, 16, 18, 22, 25, 3, 30,
	  8, 12, 20, 28, 15, 17, 24, 7, 19, 27, 23, 6, 26, 5, 4, 31
	};

      v |= v >> 1; // first round down to one less than a power of 2 
      v |= v >> 2;
      v |= v >> 4;
      v |= v >> 8;
      v |= v >> 16;

      r = MultiplyDeBruijnBitPosition[(uint32_t)(v * 0x07C4ACDDU) >> 27];

      return r;

#else // Naive approach.
      unsigned int r = 0;
      while (i >>= 1) {
	r++;
      }
      return r;
#endif

#endif
    }

    static unsigned int log2ceil (size_t sz) {
      unsigned int index = bitScanReverse (sz);
      // Add one if sz is a power of two.
      if (!(sz & (sz-1))) {
	return index;
      } else {
	return index+1;
      }
    }

    static inline unsigned int getSizeClass (size_t sz) {
      sz = (sz < sizeof(double)) ? sizeof(double) : sz;
      return log2ceil (sz) - 3;
#if 0
      size_t v = sizeof(double);
      int sc = 0;
      while (v < sz) {
	sc++;
	v <<= 1;
      }
      return sc;
#endif
    }

    static inline size_t getClassSize (const unsigned int i) {
      assert (i >= 0);
      return (sizeof(double) << i);
    }

  private:

    sassert<(BIG_OBJECT > 0)> verifyHeaderSize;
  };

}



#endif
