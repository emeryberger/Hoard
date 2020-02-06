// -*- C++ -*-

/*

  The Hoard Multiprocessor Memory Allocator
  www.hoard.org

  Author: Emery Berger, http://www.emeryberger.com
  Copyright (c) 1998-2020 Emery Berger

  See the LICENSE file at the top-level directory of this
  distribution and at http://github.com/emeryberger/Hoard.

*/

#ifndef HOARD_EMPTYHOARDMANAGER_H
#define HOARD_EMPTYHOARDMANAGER_H

#include "basehoardmanager.h"

#include "heaplayers.h"
// #include "sassert.h"

namespace Hoard {

  template <class SuperblockType_>
    class EmptyHoardManager : public BaseHoardManager<SuperblockType_> {
  public:

    EmptyHoardManager (void)
      : _magic (0x1d2d3d40)
      {}

    int isValid (void) const {
      return (_magic == 0x1d2d3d40);
    }

    typedef SuperblockType_ SuperblockType;

    void free (void *) { abort(); }
    void lock (void) {}
    void unlock (void) {}

    SuperblockType * get (size_t, EmptyHoardManager *) { abort(); return nullptr; }
    void put (SuperblockType *, size_t) { abort(); }

  private:

    unsigned long _magic;

  };

}
#endif
