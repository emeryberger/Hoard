// -*- C++ -*-

/*

  The Hoard Multiprocessor Memory Allocator
  www.hoard.org

  Author: Emery Berger, http://www.emeryberger.com
  Copyright (c) 1998-2020 Emery Berger

  See the LICENSE file at the top-level directory of this
  distribution and at http://github.com/emeryberger/Hoard.

*/

#ifndef HOARD_BASEHOARDMANAGER_H
#define HOARD_BASEHOARDMANAGER_H

/**
 * @class BaseHoardManager
 * @brief The top of the hoard manager hierarchy.
 *
 */

#include "heaplayers.h"

namespace Hoard {

  template <class SuperblockType_>
  class BaseHoardManager {
  public:

    BaseHoardManager (void)
      : _magic (0xedded00d),
        _ownerThreadId (0),
        _isActive (true)
    {
      static_assert((SuperblockSize & (SuperblockSize - 1)) == 0,
		    "Size of superblock must be a power of two.");
    }

    inline int isValid (void) const {
      return (_magic == 0xedded00d);
    }

    /// @brief Check if current thread owns this heap (for same-thread optimization).
    /// @return true if current thread is the owner, false otherwise.
    inline bool isCurrentThreadOwner() const {
      return _ownerThreadId == HL::CPUInfo::getThreadId();
    }

    /// @brief Set the owning thread ID for this heap.
    /// @param tid Thread ID of the owner.
    inline void setOwnerThreadId(unsigned int tid) {
      _ownerThreadId = tid;
    }

    /// @brief Get the owning thread ID.
    inline unsigned int getOwnerThreadId() const {
      return _ownerThreadId;
    }

    // Export the superblock type.
    typedef SuperblockType_ SuperblockType;

    /// Free an object.
    inline virtual void free (void *) {}

    /// Lock this memory manager.
    inline virtual void lock (void) {}

    /// Unlock this memory manager.
    inline virtual void unlock (void) {};

    /// Return the size of an object.
    static inline size_t getSize (void * ptr) {
      SuperblockType * s = getSuperblock (ptr);
      assert (s->isValidSuperblock());
      return s->getSize (ptr);
    }

    /// @brief Find the superblock corresponding to a pointer via bitmasking.
    /// @note  All superblocks <em>must</em> be naturally aligned, and powers of two.

    static inline SuperblockType * getSuperblock (void * ptr) {
      return SuperblockType::getSuperblock (ptr);
    }

  private:

    enum { SuperblockSize = sizeof(SuperblockType) };

    const unsigned long _magic;

    /// Thread ID of the owning thread (for same-thread detection in RedirectFree).
    unsigned int _ownerThreadId;

    /// Whether this heap is currently active (owned by a live thread).
    /// Used for superblock reclaim optimization on cross-thread frees.
    bool _isActive;

  public:
    /// @brief Check if this heap is active (owned by a live thread).
    inline bool isActive() const {
      return _isActive;
    }

    /// @brief Mark this heap as active or inactive.
    inline void setActive(bool active) {
      _isActive = active;
    }

  };

}

#endif
