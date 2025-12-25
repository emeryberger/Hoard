// -*- C++ -*-

/*

  The Hoard Multiprocessor Memory Allocator
  www.hoard.org

  Author: Emery Berger, http://www.emeryberger.com
  Copyright (c) 1998-2020 Emery Berger

  See the LICENSE file at the top-level directory of this
  distribution and at http://github.com/emeryberger/Hoard.

*/

#ifndef HOARD_EMPTYCLASS_H
#define HOARD_EMPTYCLASS_H

#include <mutex>

#include "check.h"
#include "array.h"
#include "heaplayers.h"

/**
 * @class EmptyClass
 * @brief Maintains superblocks organized by emptiness.
 */

namespace Hoard {

  template <class SuperblockType_,
	    int EmptinessClasses>
  class EmptyClass {

    enum { SuperblockSize = sizeof(SuperblockType_) };

  public:

    typedef SuperblockType_ SuperblockType;

    EmptyClass()
    {
      for (auto i = 0; i <= EmptinessClasses + 1; i++) {
	_available(i) = 0;
      }
    }

    void dumpStats() {
      for (int i = 0; i <= EmptinessClasses + 1; i++) {
	auto * s = _available(i);
	if (s) {
	  //	fprintf (stderr, "EmptyClass: emptiness class = %d\n", i);
	  while (s) {
	    s->dumpStats();
	    s = s->getNext();
	  }
	}
      }
    }

    SuperblockType * getEmpty() {
      Check<EmptyClass, MyChecker> check (this);
      auto * s = _available(0);
      if (s && 
	  (s->getObjectsFree() == s->getTotalObjects())) {
	// Got an empty one. Remove it.
	_available(0) = s->getNext();
	if (_available(0)) {
	  _available(0)->setPrev (0);
	}
	s->setPrev (0);
	s->setNext (0);
	return s;
      }
      return 0;
    }

    SuperblockType * get() {
      Check<EmptyClass, MyChecker> check (this);
      // Return as empty a superblock as possible
      // by iterating from the emptiest to the fullest available class.
      for (auto n = 0; n < EmptinessClasses + 1; n++) {
	auto * s = _available(n);
	while (s) {
	  assert (s->isValidSuperblock());
	  // Got one. Remove it.
	  _available(n) = s->getNext();
	  if (_available(n)) {
	    _available(n)->setPrev (0);
	  }
	  s->setPrev (0);
	  s->setNext (0);

#ifndef NDEBUG
	  // Verify that this superblock is *gone* from the lists.
	  for (int z = 0; z < EmptinessClasses + 1; z++) {
	    auto * p = _available(z);
	    while (p) {
	      assert (p != s);
	      p = p->getNext();
	    }
	  }
#endif

	  // Ensure that we return a superblock that is as free as
	  // possible.
	  auto cl = getFullness (s);
	  if (cl > n) {
	    put (s);
	    SuperblockType * sNew = _available(n);
	    assert (s != sNew);
	    s = sNew;
	  } else {
	    return s;
	  }
	}
      }
      return 0;
    }

    void put (SuperblockType * s) {
      Check<EmptyClass, MyChecker> check (this);

#ifndef NDEBUG
      // Check to verify that this superblock is not already on one of the lists.
      for (int n = 0; n <= EmptinessClasses + 1; n++) {
	auto * p = _available(n);
	while (p) {
	  if (p == s) {
	    abort();
	  }
	  p = p->getNext();
	}
      }
#endif

      // Put on the appropriate available list.
      auto cl = getFullness (s);

      //    printf ("put %x, cl = %d\n", s, cl);
      s->setPrev (0);
      s->setNext (_available(cl));
      if (_available(cl)) {
	_available(cl)->setPrev (s);
      }
      _available(cl) = s;
    }

    INLINE MALLOC_FUNCTION void * malloc (size_t sz) {
      // Malloc from the fullest superblock first.
      for (auto i = EmptinessClasses; i >= 0; i--) {
	SuperblockType * s = _available(i);
	// printf ("i\n");
	if (s) {
	  auto oldCl = getFullness (s);
	  void * ptr = s->malloc (sz);
	  auto newCl = getFullness (s);
	  if (ptr) {
	    if (oldCl != newCl) {
	      transfer (s, oldCl, newCl);
	    }
	    assert ((size_t) ptr % SuperblockType::Alignment == 0);
	    return ptr;
	  }
	}
      }
      return nullptr;
    }

    INLINE void free (void * ptr) {
      Check<EmptyClass, MyChecker> check (this);
      auto * s = getSuperblock (ptr);
      auto oldCl = getFullness (s);
      s->free (ptr);
      auto newCl = getFullness (s);

      if (oldCl != newCl) {
	// Transfer.
	transfer (s, oldCl, newCl);
      }
    }

    /// Find the superblock (by bit-masking) that holds a given pointer.
    static INLINE SuperblockType * getSuperblock (void * ptr) {
      return SuperblockType::getSuperblock (ptr);
    }

    /**
     * @brief Drain delayed frees from all superblocks (opportunistic).
     * @param inUseCount Pointer to in-use count to decrement (or nullptr).
     * @return Total number of objects freed from all superblocks.
     *
     * Called during malloc to process cross-thread frees pushed to
     * superblocks via pushDelayedFree(). Iterates through emptiness
     * classes and drains any pending delayed frees.
     */
    inline unsigned int drainDelayedFrees(unsigned int* inUseCount) {
      unsigned int totalFreed = 0;
      // Iterate from fullest to emptiest (matching malloc order)
      for (int i = EmptinessClasses; i >= 0; i--) {
        SuperblockType * s = _available(i);
        while (s) {
          // Save next pointer before potential transfer
          SuperblockType * nextSb = s->getNext();

          if (s->hasDelayedFrees()) {
            auto oldCl = getFullness(s);
            unsigned int freed = s->drainDelayedFrees();
            if (freed > 0) {
              totalFreed += freed;
              // Update in-use count if provided
              if (inUseCount) {
                *inUseCount -= freed;
              }
              // Check if emptiness class changed
              auto newCl = getFullness(s);
              if (oldCl != newCl) {
                transfer(s, oldCl, newCl);
              }
            }
          }
          s = nextSb;
        }
      }
      return totalFreed;
    }

    /**
     * @brief Remove a superblock from this EmptyClass's bins.
     * @param s The superblock to remove.
     * @return true if successfully removed, false if not found.
     *
     * Used for superblock reclaim when transferring ownership from
     * an inactive heap to an active one.
     */
    bool removeSuperblock(SuperblockType* s) {
      if (!s) return false;

      // Find which emptiness class it's in
      int cl = getFullness(s);

      // Verify it's actually in our bins by walking the list
      SuperblockType* cur = _available(cl);
      bool found = false;
      while (cur) {
        if (cur == s) {
          found = true;
          break;
        }
        cur = cur->getNext();
      }

      if (!found) return false;

      // Unlink from the doubly-linked list
      auto* prev = s->getPrev();
      auto* next = s->getNext();
      if (prev) { prev->setNext(next); }
      if (next) { next->setPrev(prev); }
      if (s == _available(cl)) {
        _available(cl) = next;
      }
      s->setPrev(nullptr);
      s->setNext(nullptr);

      return true;
    }

  private:

    void transfer (SuperblockType * s, int oldCl, int newCl)
    {
      auto * prev = s->getPrev();
      auto * next = s->getNext();
      if (prev) { prev->setNext (next); }
      if (next) { next->setPrev (prev); }
      if (s == _available(oldCl)) {
	assert (prev == 0);
	_available(oldCl) = next;
      }
      s->setNext (_available(newCl));
      s->setPrev (0);
      if (_available(newCl)) { _available(newCl)->setPrev (s); }
      _available(newCl) = s;
    }

    static INLINE int getFullness (SuperblockType * s) {
      // Completely full = EmptinessClasses + 1
      // Completely empty (all available) = 0
      auto total = s->getTotalObjects();
      auto free = s->getObjectsFree();
      if (total == free) {
	return 0;
      } else {
	return 1 + (int) ((EmptinessClasses * (total - free)) / total);
      }
    }

    /// Forward declarations for the sanity checker.
    /// @sa Check
    class MyChecker;
    friend class MyChecker;

    /// Precondition and postcondition checking.
    class MyChecker {
    public:
#ifndef NDEBUG
      static void precondition (EmptyClass * e) {
	e->sanityCheckPre();
      }
      static void postcondition (EmptyClass * e) {
	e->sanityCheck();
      }
#else
      static void precondition (EmptyClass *) {}
      static void postcondition (EmptyClass *) {}
#endif
    };

    void sanityCheckPre() { sanityCheck(); }

    void sanityCheck() {
      for (int i = 0; i <= EmptinessClasses + 1; i++) {
	SuperblockType * s = _available(i);
	while (s) {
	  assert (getFullness(s) == i);
	  s = s->getNext();
	}
      }
    }

    /// Per-bin lock for thread-safe list operations.
    HL::SpinLock _listLock;

    /// The bins of superblocks, by emptiness class.
    /// @note index 0 = completely empty, EmptinessClasses + 1 = full
    Array<EmptinessClasses + 2, SuperblockType *> _available;

  public:
    // ========== Thread-Safe Operations (with internal locking) ==========

    /// Acquire the bin lock.
    void lock() { _listLock.lock(); }

    /// Release the bin lock.
    void unlock() { _listLock.unlock(); }

    /// Thread-safe put (acquires lock internally).
    void putLocked(SuperblockType* s) {
      std::lock_guard<HL::SpinLock> l(_listLock);
      put(s);
    }

    /// Thread-safe get (acquires lock internally).
    SuperblockType* getLocked() {
      std::lock_guard<HL::SpinLock> l(_listLock);
      return get();
    }

    /// Transfer superblock between emptiness classes (caller must hold lock).
    void transferUnlocked(SuperblockType* s, int oldCl, int newCl) {
      transfer(s, oldCl, newCl);
    }

    /// Free to superblock and handle transfer (caller must hold lock).
    void freeUnlocked(void* ptr) {
      auto* s = getSuperblock(ptr);
      auto oldCl = getFullness(s);
      s->free(ptr);
      auto newCl = getFullness(s);
      if (oldCl != newCl) {
        transfer(s, oldCl, newCl);
      }
    }

    /// Get fullness class of superblock (for external use).
    static int getFullnessClass(SuperblockType* s) {
      return getFullness(s);
    }

  };

}


#endif
