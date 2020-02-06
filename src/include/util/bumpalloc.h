// -*- C++ -*-

/*

  The Hoard Multiprocessor Memory Allocator
  www.hoard.org

  Author: Emery Berger, http://www.emeryberger.com
  Copyright (c) 1998-2020 Emery Berger

  See the LICENSE file at the top-level directory of this
  distribution and at http://github.com/emeryberger/Hoard.

*/

#ifndef HOARD_BUMPALLOC_H
#define HOARD_BUMPALLOC_H

/**
 * @class BumpAlloc
 * @brief Obtains memory in chunks and bumps a pointer through the chunks.
 * @author Emery Berger <http://www.emeryberger.com>
 */

#include "mallocinfo.h"


namespace Hoard {

  template <int ChunkSize,
    class SuperHeap>
    class BumpAlloc : public SuperHeap {
  public:

    enum { Alignment = HL::MallocInfo::Alignment };

    BumpAlloc()
      : _bump (nullptr),
	_remaining (0)
    {}

    inline void * malloc (size_t sz) {
      if (sz < HL::MallocInfo::MinSize) {
	sz = HL::MallocInfo::MinSize;
      }
      sz = HL::align<HL::MallocInfo::Alignment>(sz);
      // If there's not enough space left to fulfill this request, get
      // another chunk.
      if (_remaining < sz) {
	refill(sz);
      }
      if (_bump) {
	char * old = _bump;
	_bump += sz;
	_remaining -= sz;
	assert ((size_t) old % Alignment == 0);
	return old;
      } else {
	// We were unable to get memory.
	return nullptr;
      }
    }

    /// Free is disabled (we only bump, never reclaim).
    inline void free (void *) {}

  private:

    /// The bump pointer.
    char * _bump;

    /// How much space remains in the current chunk.
    size_t _remaining;

    // Get another chunk.
    void refill (size_t sz) {
      // Always get at least a ChunkSize worth of memory.
      if (sz < ChunkSize) {
	sz = ChunkSize;
      }
      _bump = (char *) SuperHeap::malloc (sz);
      assert ((size_t) _bump % Alignment == 0);
      if (_bump) {
	_remaining = sz;
      } else {
	_remaining = 0;
      }
    }

  };

}

#endif
