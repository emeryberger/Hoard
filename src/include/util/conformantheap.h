// -*- C++ -*-

/*

  The Hoard Multiprocessor Memory Allocator
  www.hoard.org

  Author: Emery Berger, http://www.emeryberger.com
  Copyright (c) 1998-2020 Emery Berger

  See the LICENSE file at the top-level directory of this
  distribution and at http://github.com/emeryberger/Hoard.

*/

#ifndef HOARD_CONFORMANTHEAP_H
#define HOARD_CONFORMANTHEAP_H

namespace Hoard {

  // more concise than double declarations when the parent is complex
  // and we need the SuperHeap declaration.
  template <class Parent>
  class ConformantHeap : public Parent {
  public:
    typedef Parent SuperHeap;
  };
  
}

#endif
