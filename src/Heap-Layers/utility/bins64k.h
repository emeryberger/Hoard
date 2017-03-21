// -*- C++ -*-

/*

  Heap Layers: An Extensible Memory Allocation Infrastructure
  
  Copyright (C) 2000-2017 by Emery Berger
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
#include <stdalign.h>
#include <stddef.h>

#include "bins.h"
#include "ilog2.h"

namespace HL {

  template <class Header>
  class bins<Header, 65536> {
  public:

    bins()
    {
      static_assert(BIG_OBJECT > 0, "BIG_OBJECT must be positive.");
      static_assert(getClassSize(0) < getClassSize(1), "Need distinct size classes.");
      static_assert(getSizeClass(getClassSize(0)) == 0, "Size class computation error.");
      static_assert(getSizeClass(0) >= sizeof(max_align_t), "Min size must be at least max_align_t.");
      static_assert(getSizeClass(0) >= alignof(max_align_t), "Min size must be at least alignof(max_align_t).");
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
    //    enum { NUM_BINS = 512 };
    enum { NUM_BINS = 11 };

    static inline constexpr int getSizeClass (size_t sz) {
#if 0
      if (sz < 16) {
	sz = 16;
      } else {
	sz = ((sz + 15) & ~15UL);
      }
      return (sz >> 4) - 1;
    //
      sz = ((sz + 15) & ~15UL);
      const int szClass[] = { 0, 1, 2, 2, 3, 3, 3, 3 };
      const auto szIndex = sz >> 3;
      if (szIndex < 8) {
	return szClass[szIndex];
      }
#endif
      sz = (sz < sizeof(double)) ? sizeof(double) : sz;
      return (int) HL::ilog2(sz) - 3;
    }

    static constexpr inline size_t getClassSize (int i) {
#if 0
      assert (i >= 0);
      return (i+1) << 4;
#endif
      return (sizeof(double) << i);
    }
  };

}



#endif
