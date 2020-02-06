// -*- C++ -*-

/*

  The Hoard Multiprocessor Memory Allocator
  www.hoard.org

  Author: Emery Berger, http://www.emeryberger.com
  Copyright (c) 1998-2020 Emery Berger

  See the LICENSE file at the top-level directory of this
  distribution and at http://github.com/emeryberger/Hoard.

*/

#ifndef HOARD_ARRAY_H
#define HOARD_ARRAY_H

#include <cassert>

namespace Hoard {

  template <unsigned long N, typename T>
  class Array {
  public:

    inline T& operator()(int index) {
      assert (index < N);
      return _item[index];
    }

    inline const T& operator()(int index) const {
      assert (index < N);
      return _item[index];
    }

  private:

    T _item[N];

  };

}


#endif
