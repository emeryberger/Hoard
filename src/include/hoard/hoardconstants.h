// -*- C++ -*-

/*

  The Hoard Multiprocessor Memory Allocator
  www.hoard.org

  Author: Emery Berger, http://www.emeryberger.com
  Copyright (c) 1998-2020 Emery Berger

  See the LICENSE file at the top-level directory of this
  distribution and at http://github.com/emeryberger/Hoard.

*/

#ifndef HOARD_HOARDCONSTANTS_H
#define HOARD_HOARDCONSTANTS_H

#include <cstddef>
#include <cstdint>

namespace Hoard {

  /// Largest request Hoard will attempt to satisfy, in bytes. Any size at
  /// or below this leaves headroom for the internal additions on the
  /// allocation path -- the big-object header (AddHeaderHeap adds
  /// sizeof(Header)) and the superblock round-up in the mmap layer -- so
  /// none of them can wrap a size_t and hand back a mapping smaller than
  /// the caller was told it got (CWE-190 -> CWE-787). Requests above it are
  /// rejected (nullptr / ENOMEM). PTRDIFF_MAX is the conventional ceiling:
  /// a single object larger than it makes pointer subtraction within it
  /// undefined, and it matches glibc's "cannot allocate more than
  /// PTRDIFF_MAX" behavior.
  ///
  /// Enforced both at the xxmalloc entry point and in
  /// TheCustomHeapType::malloc (which bypasses ANSIWrapper's own guard), so
  /// the check survives even a future caller that reaches the heap by some
  /// other path.
  static constexpr size_t MaxAllocationSize = PTRDIFF_MAX;

  /// Cache line size for false sharing prevention.
  /// L1 data cache lines are 64 bytes on x86/x64 and ARM64.
  enum { CACHE_LINE_SIZE = 64 };

  /// Destructive interference size for cross-core false sharing.
  /// Apple Silicon (M1+) uses 128-byte L2 cache lines; use this for
  /// data structures where distinct threads write to adjacent fields.
#if defined(__APPLE__) && defined(__aarch64__)
  enum { DESTRUCTIVE_INTERFERENCE_SIZE = 128 };
#else
  enum { DESTRUCTIVE_INTERFERENCE_SIZE = 64 };
#endif

  /// The maximum amount of memory that each TLAB may hold, in bytes.
  enum { MAX_MEMORY_PER_TLAB = 16 * 1024 * 1024UL }; // 16MB
  
  /// The maximum number of threads supported (sort of).
  enum { MaxThreads = 2048 };
  
  /// The maximum number of heaps supported.
  enum { NumHeaps = 128 };
  
  /// Size, in bytes, of the largest object we will cache on a
  /// thread-local allocation buffer.
  enum { LargestSmallObject = 1024UL };
    
}

#endif
