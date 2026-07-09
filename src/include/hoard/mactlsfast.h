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
 * @file   mactlsfast.h
 * @brief  Inline macOS TLS fast path for the per-thread heap pointer.
 *
 * getCustomHeap() sits on every malloc/free. Defining its fast path
 * inline here (rather than out-of-line in mactls.cpp) removes a call
 * from the hot path: the common case is a thread-pointer read, one
 * load, and a predicted branch.
 *
 * We directly access an unused pthread TLS slot (slot 89, same slot
 * mimalloc uses) because __thread variables on macOS always go through
 * _tlv_get_addr, which is slow. See mactls.cpp for details, including
 * the ARM64 tpidrro_el0 quirk.
 */

#ifndef HOARD_MACTLSFAST_H
#define HOARD_MACTLSFAST_H

#if !defined(__APPLE__)
#error "This file is intended for use with MacOS systems only."
#endif

#include <pthread.h>

#include "hoardtlab.h"

#define HOARD_TLS_SLOT 89

#if defined(__x86_64__)

static inline TheCustomHeapType* hoardGetTlsHeap() {
  void* res;
  const size_t ofs = HOARD_TLS_SLOT * sizeof(void*);
  __asm__ volatile("movq %%gs:%1, %0" : "=r" (res) : "m" (*((void**)ofs)));
  return reinterpret_cast<TheCustomHeapType*>(res);
}

static inline void hoardSetTlsHeap(TheCustomHeapType* value) {
  const size_t ofs = HOARD_TLS_SLOT * sizeof(void*);
  __asm__ volatile("movq %1, %%gs:%0" : "=m" (*((void**)ofs)) : "r" ((void*)value));
}

#elif defined(__aarch64__)

static inline TheCustomHeapType* hoardGetTlsHeap() {
  void** tcb;
  __asm__ volatile("mrs %0, tpidrro_el0\n\tbic %0, %0, #7" : "=r" (tcb));
  return reinterpret_cast<TheCustomHeapType*>(tcb[HOARD_TLS_SLOT]);
}

static inline void hoardSetTlsHeap(TheCustomHeapType* value) {
  void** tcb;
  __asm__ volatile("mrs %0, tpidrro_el0\n\tbic %0, %0, #7" : "=r" (tcb));
  tcb[HOARD_TLS_SLOT] = value;
}

#else
#error "Unsupported macOS architecture (expected x86_64 or arm64)."
#endif

/// Out-of-line slow path: first access on a thread (see mactls.cpp).
TheCustomHeapType * hoardSlowGetCustomHeap();

inline TheCustomHeapType * getCustomHeap() {
  TheCustomHeapType * heap = hoardGetTlsHeap();
  if (__builtin_expect(heap != nullptr, 1)) {
    return heap;
  }
  return hoardSlowGetCustomHeap();
}

#endif // HOARD_MACTLSFAST_H
