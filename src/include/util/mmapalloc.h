// -*- C++ -*-

/*

  The Hoard Multiprocessor Memory Allocator
  www.hoard.org

  Author: Emery Berger, http://www.emeryberger.com
  Copyright (c) 1998-2020 Emery Berger

  See the LICENSE file at the top-level directory of this
  distribution and at http://github.com/emeryberger/Hoard.

*/

#ifndef HOARD_MMAPALLOC_H
#define HOARD_MMAPALLOC_H

#include "heaplayers.h"

/**
 * @class MmapAlloc
 * @brief Obtains memory from Mmap but doesn't allow it to be freed.
 * @author Emery Berger <http://www.cs.umass.edu/~emery>
 */

namespace Hoard {
  
  class MmapAlloc {
  public:
    
    enum { Alignment = HL::MmapWrapper::Alignment };
    
    static void * malloc (size_t sz) {
      void * ptr = HL::MmapWrapper::map (sz);
      return ptr;
    }

  };
  
}

#endif
