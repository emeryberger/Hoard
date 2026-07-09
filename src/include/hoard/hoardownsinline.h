// -*- C++ -*-

/*

  The Hoard Multiprocessor Memory Allocator
  www.hoard.org

  Author: Emery Berger, http://www.emeryberger.com
  Copyright (c) 1998-2026 Emery Berger

  See the LICENSE file at the top-level directory of this
  distribution and at http://github.com/emeryberger/Hoard.

*/

/**
 * @file   hoardownsinline.h
 * @brief  Inline ownership predicate for the alloc8 interposition layer.
 *
 * Included by alloc8's macOS wrapper via ALLOC8_XXOWNS_INLINE_HEADER
 * (see CMakeLists.txt). Must define alloc8_xxowns_inline(): true iff
 * the pointer was issued by Hoard. Safe on any address; no false
 * positives or negatives. This is the compile-time twin of xxowns()
 * in libhoard.cpp — alloc8 inlines it directly into replace_free /
 * replace_realloc / malloc_size, avoiding a function call per free
 * (the runtime xxowns() hook cannot inline: alloc8's weak module-local
 * default blocks cross-module inlining under ThinLTO).
 *
 * Keep in sync with xxowns() in libhoard.cpp.
 */

#ifndef HOARD_OWNSINLINE_H
#define HOARD_OWNSINLINE_H

#include <cstddef>

#include "ownershipmap.h"

// Must match SUPERBLOCK_SIZE (hoardheap.h) for non-Windows builds;
// statically checked in libhoard.cpp.
#define HOARD_OWNS_CHUNK_SIZE 262144UL

// Bounds of the static buffer that serves allocations made before the
// heap is initialized (defined in libhoard.cpp; constant-initialized,
// so safe to read from arbitrarily early interposed calls).
extern "C" {
  extern const char * const hoardInitBufferStart;
  extern const char * const hoardInitBufferEnd;
}

static inline bool alloc8_xxowns_inline (const void * ptr) {
  if (__builtin_expect(
        Hoard::OwnershipMap<HOARD_OWNS_CHUNK_SIZE>::contains (ptr), 1)) {
    return true;
  }
  auto * p = reinterpret_cast<const char *>(ptr);
  return (p >= hoardInitBufferStart && p < hoardInitBufferEnd);
}

#endif // HOARD_OWNSINLINE_H
