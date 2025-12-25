// -*- C++ -*-

/*

  The Hoard Multiprocessor Memory Allocator
  www.hoard.org

  Author: Emery Berger, http://www.emeryberger.com
  Copyright (c) 1998-2020 Emery Berger

  See the LICENSE file at the top-level directory of this
  distribution and at http://github.com/emeryberger/Hoard.

*/

/**
 * @file manageonesuperblock.h
 * @author Emery Berger <http://www.emeryberger.com>
 */


#ifndef HOARD_MANAGEONESUPERBLOCK_H
#define HOARD_MANAGEONESUPERBLOCK_H

/**
 * @class  ManageOneSuperblock
 * @brief  A layer that caches exactly one superblock, thus avoiding costly lookups.
 * @author Emery Berger <http://www.cs.umass.edu/~emery>
 */

// Branch prediction hints for hot paths
#if defined(__GNUC__) || defined(__clang__)
#define likely(x) __builtin_expect(!!(x), 1)
#define unlikely(x) __builtin_expect(!!(x), 0)
#elif defined(_MSC_VER) && _MSC_VER >= 1925 && defined(__cplusplus) && __cplusplus >= 202002L
// MSVC 2019 16.5+ with C++20: use standard attributes (must be used differently)
// Note: C++20 [[likely]]/[[unlikely]] are statement attributes, not expression wrappers
// For MSVC, we fall back to assume intrinsic for critical paths
#define likely(x) (x)
#define unlikely(x) (x)
// Use __assume for optimizer hints in critical sections where applicable:
// if (cond) { __assume(cond); ... }
#else
// Fallback: no hints available
#define likely(x) (x)
#define unlikely(x) (x)
#endif

namespace Hoard {

  template <class SuperHeap>
  class ManageOneSuperblock : public SuperHeap {
  public:

    ManageOneSuperblock()
      : _current (nullptr)
    {}

    typedef typename SuperHeap::SuperblockType SuperblockType;

    /// Get memory from the current superblock.
    inline void * malloc (size_t sz) {
      if (likely(_current)) {
	void * ptr = _current->malloc (sz);
	if (ptr) {
	  assert (_current->getSize(ptr) >= sz);
	  return ptr;
	}
      }
      // No memory -- get another superblock.
      return slowMallocPath (sz);
    }

    /// Try to free the pointer to this superblock first.
    inline void free (void * ptr) {
      SuperblockType * s = SuperHeap::getSuperblock (ptr);
      if (likely(s == _current)) {
	_current->free (ptr);
      } else {
	// It wasn't ours, so free it remotely.
	SuperHeap::free (ptr);
      }
    }

    /// Get the current superblock and remove it.
    SuperblockType * get() {
      if (likely(_current)) {
	SuperblockType * s = _current;
	_current = nullptr;
	return s;
      } else {
	// There's none cached, so just get one from the superheap.
	return SuperHeap::get();
      }
    }

    /// Put the superblock into the cache.
    inline void put (SuperblockType * s) {
      if (!s || (s == _current) || (!s->isValidSuperblock())) {
	// Ignore if we already are holding this superblock, of if we
	// got a nullptr, or if it's invalid.
	return;
      }
      if (_current) {
	// We have one already -- push it out.
	SuperHeap::put (_current);
      }
      _current = s;
    }

  private:

    /// Obtain a superblock and return an object from it.
    void * slowMallocPath (size_t sz) {
      void * ptr = nullptr;
      while (!ptr) {
	// If we don't have a superblock, get one.
	if (!_current) {
	  _current = SuperHeap::get();
	  if (!_current) {
	    // Out of memory.
	    return nullptr;
	  }
	}
	// Try to allocate memory from it.
	ptr = _current->malloc (sz);
	if (!ptr) {
	  // No memory left: put the superblock away and get a new one next time.
	  SuperHeap::put (_current);
	  _current = nullptr;
	}
      }
      return ptr;
    }

    /// The current superblock.
    SuperblockType * _current;

  };

}

#endif
