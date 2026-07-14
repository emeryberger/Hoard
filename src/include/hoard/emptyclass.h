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

#if defined(_MSC_VER)
#include <intrin.h>
static inline int hoard_clz(unsigned int x) {
  unsigned long idx;
  _BitScanReverse(&idx, x);
  return 31 - (int)idx;
}
#else
#define hoard_clz(x) __builtin_clz(x)
#endif

#include "check.h"
#include "array.h"

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
      : _binmap(0)
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
	} else {
	  _binmap &= ~(1U << 0);
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
	  } else {
	    _binmap &= ~(1U << n);
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

      s->setPrev (0);
      s->setNext (_available(cl));
      if (_available(cl)) {
	_available(cl)->setPrev (s);
      }
      _available(cl) = s;
      _binmap |= (1U << cl);
    }

    INLINE MALLOC_FUNCTION void * malloc (size_t sz) {
      // Malloc from the fullest superblock first.
      // Use bitmap to find the fullest non-empty class in O(1).
      // Mask out the "full" bin (EmptinessClasses+1) and any higher bits.
      auto allocBits = _binmap & ((1U << (EmptinessClasses + 1)) - 1);
      while (allocBits) {
	// Find the highest set bit = fullest non-empty class.
	int i = 31 - hoard_clz(allocBits);
	SuperblockType * s = _available(i);
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
	// This class didn't yield an allocation; clear the bit and try next.
	allocBits &= ~(1U << i);
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

    /// Free n objects that ALL belong to superblock s, doing the emptiness
    /// class bookkeeping ONCE for the whole run instead of once per object.
    /// Fullness is a function of the superblock's free count, so a run of
    /// frees can only move it in one direction: computing it before and after
    /// is equivalent to recomputing it every time, and a single transfer
    /// lands the superblock in the same class.
    INLINE void freeRun (SuperblockType * s, void ** objs, size_t n) {
      Check<EmptyClass, MyChecker> check (this);
      auto oldCl = getFullness (s);
      for (size_t i = 0; i < n; i++) {
	s->free (objs[i]);
      }
      auto newCl = getFullness (s);

      if (oldCl != newCl) {
	transfer (s, oldCl, newCl);
      }
    }

    /// Find the superblock (by bit-masking) that holds a given pointer.
    static INLINE SuperblockType * getSuperblock (void * ptr) {
      return SuperblockType::getSuperblock (ptr);
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
	if (!next) {
	  _binmap &= ~(1U << oldCl);
	}
      }
      s->setNext (_available(newCl));
      s->setPrev (0);
      if (_available(newCl)) { _available(newCl)->setPrev (s); }
      _available(newCl) = s;
      _binmap |= (1U << newCl);
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

    /// Bitmap for O(1) lookup of non-empty bins.
    /// Bit i is set iff _available(i) != nullptr.
    unsigned int _binmap;

    /// The bins of superblocks, by emptiness class.
    /// @note index 0 = completely empty, EmptinessClasses + 1 = full
    Array<EmptinessClasses + 2, SuperblockType *> _available;

  };

}


#endif
