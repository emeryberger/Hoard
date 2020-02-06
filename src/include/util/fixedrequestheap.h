// -*- C++ -*-

/*

  The Hoard Multiprocessor Memory Allocator
  www.hoard.org

  Author: Emery Berger, http://www.emeryberger.com
  Copyright (c) 1998-2020 Emery Berger

  See the LICENSE file at the top-level directory of this
  distribution and at http://github.com/emeryberger/Hoard.

*/

#ifndef HOARD_FIXEDREQUESTHEAP_H
#define HOARD_FIXEDREQUESTHEAP_H

/**
 * @class FixedRequestHeap
 * @brief Always grabs the same size, regardless of the request size.
 */

namespace Hoard {
  
  template <size_t RequestSize,
	    class SuperHeap>
  class FixedRequestHeap : public SuperHeap {
  public:
    inline void * malloc (size_t) {
      return SuperHeap::malloc (RequestSize);
    }
    inline static size_t getSize (void *) {
      return RequestSize;
    }
  };

}

#endif
