// -*- C++ -*-

/*

  The Hoard Multiprocessor Memory Allocator
  www.hoard.org

  Author: Emery Berger, http://www.emeryberger.com
  Copyright (c) 1998-2020 Emery Berger

  See the LICENSE file at the top-level directory of this
  distribution and at http://github.com/emeryberger/Hoard.

*/

#ifndef HOARD_STATISTICS_H
#define HOARD_STATISTICS_H

#include "hoardconstants.h"

namespace Hoard {

  /**
   * @class Statistics
   * @brief Tracks in-use and allocated object counts per size class.
   *
   * Padded to cache line size to prevent false sharing between adjacent
   * bins' statistics in the per-bin array. Note: we use padding instead
   * of alignas() because alignas on class types can cause issues with
   * array allocation under certain compiler optimizations.
   */
  class Statistics {
  public:
    Statistics()
      : _inUse(0),
        _allocated(0)
    {}

    inline unsigned int getInUse() const { return _inUse; }
    inline unsigned int getAllocated() const { return _allocated; }
    inline void setInUse(unsigned int u) { _inUse = u; }
    inline void setAllocated(unsigned int a) { _allocated = a; }

  private:
    /// The number of objects in use.
    unsigned int _inUse;

    /// The number of objects allocated.
    unsigned int _allocated;

    /// Padding to fill cache line, preventing false sharing.
    char _pad[CACHE_LINE_SIZE - 2 * sizeof(unsigned int)];
  };

}

#endif
