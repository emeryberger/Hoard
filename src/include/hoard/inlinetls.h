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
 * @file   inlinetls.h
 * @brief  Inline TLS access for maximum malloc/free performance.
 *
 * This header provides inline TLS access to the thread-local heap,
 * eliminating function call overhead on the allocation fast path.
 * The TLS variable uses initial-exec model for direct access.
 */

#ifndef HOARD_INLINETLS_H
#define HOARD_INLINETLS_H

#include "hoardtlab.h"

// TLS model for fast access
#if !defined(INITIAL_EXEC_ATTR)
#if !defined(__APPLE__)
#define INITIAL_EXEC_ATTR __attribute__((tls_model ("initial-exec")))
#else
#define INITIAL_EXEC_ATTR
#endif
#endif

// The TLS pointer must be declared inline for each compilation unit
// that needs it. On Linux with initial-exec, this compiles to direct
// fs: segment access (~3 cycles) rather than a function call (~20 cycles).

#if defined(__linux__) || defined(__FreeBSD__) || defined(__NetBSD__)

extern __thread TheCustomHeapType * theTLAB INITIAL_EXEC_ATTR;

// Defined in unixtls.cpp
extern TheCustomHeapType * initializeCustomHeap();

// Inline fast path: direct TLS access, no function call.
// The cold path (initialization) calls out of line.
static inline TheCustomHeapType * getCustomHeapInline() {
  TheCustomHeapType * tlab = theTLAB;
  if (__builtin_expect(tlab != nullptr, 1)) {
    return tlab;
  }
  return initializeCustomHeap();
}

#define getCustomHeap getCustomHeapInline

#else

// On other platforms (macOS, etc.), use the regular function.
// macOS has different TLS semantics where initial-exec doesn't help.
extern TheCustomHeapType * getCustomHeap();

#endif

#endif // HOARD_INLINETLS_H
