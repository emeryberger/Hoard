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

#include <atomic>
#include <cstddef>

#include "hoardconstants.h"

namespace Hoard {

  /**
   * @class Statistics
   * @brief Tracks in-use and allocated object counts with atomic operations.
   *
   * Uses relaxed memory ordering for eventual consistency. This is safe
   * because the threshold function has hysteresis (2*SUPERBLOCK_SIZE objects)
   * which far exceeds any temporary inconsistency from concurrent operations.
   *
   * Padded to 64 bytes to prevent false sharing between adjacent bins'
   * statistics in the per-bin array. Uses 64 bytes (L1 cache line size)
   * on all platforms, since bins within a single HoardManager are
   * typically accessed by the same thread.
   */
  class alignas(64) Statistics {
  public:
    Statistics()
      : _inUse(0),
        _allocated(0)
    {}

    /// Get current in-use count (relaxed read).
    inline unsigned int getInUse() const {
      return _inUse.load(std::memory_order_relaxed);
    }

    /// Get current allocated count (relaxed read).
    inline unsigned int getAllocated() const {
      return _allocated.load(std::memory_order_relaxed);
    }

    /// Atomically increment in-use count (for allocation).
    inline void incrementInUse() {
      _inUse.fetch_add(1, std::memory_order_relaxed);
    }

    /// Atomically decrement in-use count (for free).
    inline void decrementInUse() {
      _inUse.fetch_sub(1, std::memory_order_relaxed);
    }

    /// Bulk adjustment for superblock transfer.
    /// @param inUseDelta Objects in use to add (negative to subtract)
    /// @param allocatedDelta Total objects to add (negative to subtract)
    inline void adjustForSuperblock(int inUseDelta, int allocatedDelta) {
      if (inUseDelta > 0) {
        _inUse.fetch_add(static_cast<unsigned int>(inUseDelta), std::memory_order_relaxed);
      } else if (inUseDelta < 0) {
        _inUse.fetch_sub(static_cast<unsigned int>(-inUseDelta), std::memory_order_relaxed);
      }
      if (allocatedDelta > 0) {
        _allocated.fetch_add(static_cast<unsigned int>(allocatedDelta), std::memory_order_relaxed);
      } else if (allocatedDelta < 0) {
        _allocated.fetch_sub(static_cast<unsigned int>(-allocatedDelta), std::memory_order_relaxed);
      }
    }

    /// Set in-use count (for compatibility with existing code).
    inline void setInUse(unsigned int u) {
      _inUse.store(u, std::memory_order_relaxed);
    }

    /// Set allocated count (for compatibility with existing code).
    inline void setAllocated(unsigned int a) {
      _allocated.store(a, std::memory_order_relaxed);
    }

  private:
    /// The number of objects in use (atomic for lock-free updates).
    std::atomic<unsigned int> _inUse;

    /// The number of objects allocated (atomic for lock-free updates).
    std::atomic<unsigned int> _allocated;

    /// Padding to fill one 64-byte L1 cache line, preventing false sharing.
    char _pad[64 - 2 * sizeof(std::atomic<unsigned int>)];
  };

}

#endif
