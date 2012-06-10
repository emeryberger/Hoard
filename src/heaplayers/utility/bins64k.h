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
      // FIX ME processor dependent
      union {
	double d;
	struct {
	  unsigned int mantissal : 32;
	  unsigned int mantissah : 20;
	  unsigned int exponent : 11;
	  unsigned int sign : 1;
	};
      } ud;
      ud.d = (double)(i & ~(i >> 32));  // avoid rounding error
      return ud.exponent - 1023;
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
