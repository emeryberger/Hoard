// -*- C++ -*-

/*

  The Hoard Multiprocessor Memory Allocator
  www.hoard.org

  Author: Emery Berger, http://www.emeryberger.com
  Copyright (c) 1998-2026 Emery Berger

  See the LICENSE file at the top-level directory of this
  distribution and at http://github.com/emeryberger/Hoard.

*/

#ifndef HOARD_PURGE_H
#define HOARD_PURGE_H

// Decommit / discard pages so the OS can reclaim physical RAM while the
// virtual mapping survives. Called on superblock data buffers when they
// land fully empty in the global heap, mirroring jemalloc/mimalloc's
// extent/segment purging.
//
// The semantics across platforms:
//   - Linux MADV_FREE: pages may be reclaimed lazily under pressure;
//     reads see zeros only after reclaim. RSS drops without a refault
//     cost in the common reuse-soon case.
//   - Linux MADV_DONTNEED (fallback): pages reclaimed immediately and
//     refaulted on next access. Higher refault cost but works on every
//     kernel.
//   - macOS MADV_FREE: equivalent to Linux MADV_FREE.
//   - Windows DiscardVirtualMemory (Win 8.1+) / VirtualAlloc(MEM_RESET)
//     fallback: pages may be discarded; virtual mapping retained.

#include <cstddef>
#include <cstdint>

#if defined(_WIN32)
  #ifndef WIN32_LEAN_AND_MEAN
  #define WIN32_LEAN_AND_MEAN
  #endif
  #include <windows.h>
#else
  #include <sys/mman.h>
  #include <unistd.h>
  #include <errno.h>
#endif

namespace Hoard {

  // Page size (cached). Cheap to call repeatedly; the result is constant.
  inline size_t purgePageSize() {
#if defined(_WIN32)
    static size_t cached = []{
      SYSTEM_INFO si;
      GetSystemInfo(&si);
      return (size_t) si.dwPageSize;
    }();
    return cached;
#else
    static size_t cached = (size_t) sysconf(_SC_PAGESIZE);
    return cached;
#endif
  }

  /// Purge (decommit / madvise) a region. The region is rounded inward
  /// to whole pages so we never touch bytes outside [p, p+bytes).
  ///
  /// Returns true on success or when there is nothing to do; false on
  /// hard error. Soft failures (e.g. EAGAIN) are ignored — purging is
  /// always advisory.
  inline bool purgePages(void* p, size_t bytes) {
    if (!p || bytes == 0) return true;
    const size_t ps = purgePageSize();
    auto base = reinterpret_cast<uintptr_t>(p);
    auto aligned = (base + ps - 1) & ~(uintptr_t)(ps - 1);
    auto offset = aligned - base;
    if (offset >= bytes) return true;
    size_t span = (bytes - offset) & ~(uintptr_t)(ps - 1);
    if (span == 0) return true;
    void* a = reinterpret_cast<void*>(aligned);

#if defined(_WIN32)
    // Prefer DiscardVirtualMemory (Win 8.1+); falls back to MEM_RESET
    // which has the same RSS effect on older systems.
    typedef DWORD (WINAPI *PDVM)(PVOID, SIZE_T);
    static PDVM pDiscard = []() -> PDVM {
      HMODULE h = GetModuleHandleW(L"kernel32.dll");
      if (!h) return nullptr;
      return (PDVM) GetProcAddress(h, "DiscardVirtualMemory");
    }();
    if (pDiscard) {
      pDiscard(a, span);
      return true;
    }
    return VirtualAlloc(a, span, MEM_RESET, PAGE_READWRITE) != nullptr;
#elif defined(__APPLE__)
    // macOS: MADV_FREE_REUSABLE removes the pages from the process
    // footprint immediately (it is what Apple's own allocators use);
    // plain MADV_FREE leaves them counted in RSS until memory pressure.
    if (madvise(a, span, MADV_FREE_REUSABLE) == 0) return true;
    return madvise(a, span, MADV_FREE) == 0;
#else
    // Linux: prefer MADV_FREE; fall back to MADV_DONTNEED on old kernels.
  #ifdef MADV_FREE
    if (madvise(a, span, MADV_FREE) == 0) return true;
    if (errno != EINVAL) return false;
  #endif
    return madvise(a, span, MADV_DONTNEED) == 0;
#endif
  }

}

#endif
